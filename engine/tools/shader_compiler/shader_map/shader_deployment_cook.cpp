#include "shader_map/shader_deployment_cook.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <iostream>
#include <utility>

#include "asset/asset_catalog.h"
#include "asset/material/material_asset.h"
#include "asset/mesh/static_mesh_asset.h"
#include "asset/animation/animation_asset.h"
#include "asset/scene/scene_asset.h"
#include "asset/texture/texture_asset.h"
#include "file_system/directory_file_store.h"
#include "compiler/standard_surface.h"
#include "frontend/shader_parser.h"
#include "shader/shader_deployment.h"

namespace toy3d::shader
{
    namespace
    {
        constexpr std::size_t maximum_cook_source_bytes = 4u * 1024u * 1024u;
        constexpr std::size_t maximum_cook_directories = 4096u;
        struct CookSource
        {
            PhysicalPath path;
            std::string virtual_path;
            ShaderAsset asset;
            ShaderSourceCompileRequest request;
            Sha256Hash source_hash{};
        };
        bool mount(PlatformFile& platform, FileSystem& files, const PhysicalPath& path, const char* virtual_root,
                   std::string& error)
        {
            DirectoryFileStoreDesc desc;
            desc.physical_root = path;
            desc.writable = false;
            const auto store = DirectoryFileStore::create(platform, desc);
            if (!store.succeeded())
            {
                error = store.status().message;
                return false;
            }
            FileMountDesc entry;
            entry.virtual_root = VirtualPath::parse(virtual_root).value();
            entry.store = store.value();
            entry.allow_enumeration = true;
            const auto added = files.add_mount(entry);
            if (!added.succeeded())
            {
                error = added.message;
                return false;
            }
            return true;
        }
        bool discover(PlatformFile& files, const PhysicalPath& root, const std::string& virtual_root,
                      std::vector<CookSource>& sources, std::size_t& directories, std::size_t depth, std::string& error)
        {
            if (++directories > maximum_cook_directories || depth > 32u)
            {
                error = "Shader source discovery exceeds its directory/depth budget.";
                return false;
            }
            const auto entries = files.enumerate_directory(root);
            if (!entries.succeeded() || entries.value().size() > maximum_cook_directories)
            {
                error = "Shader source tree is unreadable or exceeds its enumeration budget.";
                return false;
            }
            for (const auto& entry : entries.value())
            {
                const std::string name = entry.path.utf8().substr(entry.path.utf8().find_last_of("/\\") + 1u);
                if (entry.type == FileType::Symlink)
                {
                    error = "Shader Cook refuses linked source entries: " + entry.path.utf8();
                    return false;
                }
                if (entry.type == FileType::Directory)
                {
                    if (!discover(files, entry.path, virtual_root + "/" + name, sources, directories, depth + 1u,
                                  error))
                    {
                        return false;
                    }
                }
                else if (entry.type == FileType::File && name.size() > 7u &&
                         name.compare(name.size() - 7u, 7u, ".shader") == 0)
                {
                    const auto stat = files.stat(entry.path);
                    if (sources.size() >= max_shader_build_sources || !stat.succeeded() ||
                        stat.value().size > maximum_cook_source_bytes)
                    {
                        error = "Shader Cook source count/size budget exceeded.";
                        return false;
                    }
                    const auto text = files.read_text_utf8(entry.path);
                    if (!text.succeeded())
                    {
                        error = text.status().message;
                        return false;
                    }
                    const auto parsed = parse_shader(text.value(), virtual_root + "/" + name);
                    if (!parsed.succeeded())
                    {
                        error = parsed.diagnostics.empty() ? "Shader source parse failed."
                                                           : format_diagnostic(parsed.diagnostics.front());
                        return false;
                    }
                    sources.push_back({entry.path, virtual_root + "/" + name, *parsed.asset, {}, sha256(text.value())});
                }
            }
            return true;
        }
        std::string comparable(const PlatformFile& files, const PhysicalPath& path)
        {
            auto value = path.utf8();
            std::replace(value.begin(), value.end(), '\\', '/');
            while (value.size() > 1u && value.back() == '/')
            {
                value.pop_back();
            }
            if (!files.capabilities().case_sensitive_lookup)
            {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c)
                               {
                                   return static_cast<char>(std::tolower(c));
                               });
            }
            return value;
        }
        bool overlap(const PlatformFile& files, const PhysicalPath& a, const PhysicalPath& b)
        {
            const auto left = comparable(files, a), right = comparable(files, b);
            return left == right || left.compare(0u, right.size() + 1u, right + "/") == 0 ||
                   right.compare(0u, left.size() + 1u, left + "/") == 0;
        }
        bool new_destination(PlatformFile& files, const PhysicalPath& input, PhysicalPath& output, std::string& error)
        {
            const auto parent = files.parent_path(input);
            const auto canonical =
                parent.succeeded() ? files.canonical(parent.value()) : FileResult<PhysicalPath>(parent.status());
            const auto stat = files.stat(input);
            if (!canonical.succeeded() || stat.succeeded() || stat.status().code != FileErrorCode::NotFound)
            {
                error = "Shader Cook destination must be new and have an existing parent: " + input.utf8();
                return false;
            }
            const auto name = input.utf8().substr(input.utf8().find_last_of("/\\") + 1u);
            const auto joined = files.join_relative(canonical.value(), name);
            if (!joined.succeeded())
            {
                error = joined.status().message;
                return false;
            }
            output = joined.value();
            return true;
        }
    } // namespace

    bool cook_shader_deployment(PlatformFile& platform, const ProcessService& processes,
                                const ShaderDeploymentCookInput& input, std::string& error)
    {
        const auto engine = platform.canonical(input.engine_root);
        const auto project = input.project_root.empty() ? FileResult<PhysicalPath>(PhysicalPath{})
                                                        : platform.canonical(input.project_root);
        PhysicalPath output, work;
        if (!engine.succeeded() || !project.succeeded() ||
            !new_destination(platform, input.output_root, output, error) ||
            !new_destination(platform, input.work_root, work, error))
        {
            if (error.empty())
            {
                error = "Shader Cook requires existing Engine/Project roots.";
            }
            return false;
        }
        if (overlap(platform, output, work) || overlap(platform, output, engine.value()) ||
            overlap(platform, work, engine.value()) ||
            (!project.value().empty() &&
             (overlap(platform, output, project.value()) || overlap(platform, work, project.value()))))
        {
            error = "Shader Cook destinations must not overlap source trees or each other.";
            return false;
        }
        const PhysicalPath engine_settings(engine.value().utf8() + "/config/shader_build.settings");
        const PhysicalPath project_settings =
            project.value().empty() ? PhysicalPath{}
                                    : PhysicalPath(project.value().utf8() + "/config/shader_build.settings");
        ShaderBuildSettings settings;
        if (!read_shader_build_settings(platform, engine_settings, project_settings, settings, error))
        {
            return false;
        }
        const auto settings_hash = sha256(serialize_shader_build_settings(settings));
        FileSystem files;
        TypeRegistry types;
        auto status = register_static_mesh_asset_types(types);
        if (status.succeeded())
        {
            status = register_animation_asset_types(types);
        }
        if (status.succeeded())
        {
            status = register_material_asset_types(types);
        }
        if (status.succeeded())
        {
            status = register_texture_asset_types(types);
        }
        if (status.succeeded())
        {
            status = register_scene_asset_types(types);
        }
        if (status.succeeded())
        {
            status = types.freeze();
        }
        if (!status.succeeded())
        {
            error = status.message;
            return false;
        }
        const PhysicalPath engine_include(engine.value().utf8() + "/shader/include");
        const PhysicalPath project_include(project.value().utf8() + "/shader/include");
        std::vector<VirtualPath> roots{VirtualPath::parse("/Engine").value()};
        if (!mount(platform, files, PhysicalPath(engine.value().utf8() + "/asset"), "/Engine", error) ||
            !mount(platform, files, engine_include, "/Engine/ShaderIncludes", error))
        {
            return false;
        }
        bool has_project_include = false;
        if (!project.value().empty())
        {
            roots.push_back(VirtualPath::parse("/Project").value());
            if (!mount(platform, files, PhysicalPath(project.value().utf8() + "/asset"), "/Project", error))
            {
                return false;
            }
            const auto include_stat = platform.stat(project_include);
            has_project_include = include_stat.succeeded();
            if (has_project_include && !mount(platform, files, project_include, "/Project/ShaderIncludes", error))
            {
                return false;
            }
            if (!include_stat.succeeded() && include_stat.status().code != FileErrorCode::NotFound)
            {
                error = include_stat.status().message;
                return false;
            }
        }
        if (!files.freeze().succeeded())
        {
            error = "Shader Cook mounts could not be frozen.";
            return false;
        }
        const auto catalog = scan_asset_catalog(types, files, roots, false);
        if (!catalog.succeeded())
        {
            error = catalog.status().message;
            return false;
        }
        std::vector<CookSource> sources;
        std::size_t directories = 0u;
        if (!discover(platform, PhysicalPath(engine.value().utf8() + "/shader/builtin"), "/Engine/Shaders", sources,
                      directories, 0u, error))
        {
            return false;
        }
        if (!project.value().empty())
        {
            const PhysicalPath project_source(project.value().utf8() + "/shader");
            const auto stat = platform.stat(project_source);
            if (stat.succeeded() &&
                !discover(platform, project_source, "/Project/Shaders", sources, directories, 0u, error))
            {
                return false;
            }
            if (!stat.succeeded() && stat.status().code != FileErrorCode::NotFound)
            {
                error = stat.status().message;
                return false;
            }
        }
        std::sort(sources.begin(), sources.end(),
                  [](const CookSource& a, const CookSource& b)
                  {
                      return a.asset.name < b.asset.name;
                  });
        std::set<std::string> names;
        std::map<std::string, ShaderUsage> usages;
        ShaderDeployment deployment;
        std::map<std::string, Sha256Hash> material_descriptors;
        std::vector<CookSource> required_sources;
        for (auto& source : sources)
        {
            if (!names.insert(source.asset.name).second)
            {
                error = "Duplicate Shader source name: " + source.asset.name;
                return false;
            }
            usages.emplace(source.asset.name, source.asset.usage);
            if (!input.editor && source.asset.usage == ShaderUsage::MeshPass &&
                std::all_of(source.asset.passes.begin(), source.asset.passes.end(),
                            [](const ShaderPass& pass)
                            {
                                return pass.role == ShaderPassRole::HitProxy;
                            }))
            {
                if (settings.additional_configurations.count(source.asset.name))
                {
                    error = "Player cannot request an Editor-only Shader source.";
                    return false;
                }
                continue;
            }
            std::vector<std::vector<ShaderPermutationSelection>> configurations = {{}};
            if (source.asset.usage == ShaderUsage::Material)
            {
                const auto gathered =
                    collect_material_shader_configurations(types, files, catalog.value(), source.asset.name);
                if (!gathered.succeeded())
                {
                    error = gathered.status().message;
                    return false;
                }
                configurations = gathered.value().configurations;
                material_descriptors.insert(gathered.value().descriptors.begin(), gathered.value().descriptors.end());
            }
            if (!make_shader_source_compile_request(settings, source.asset.name, ShaderTarget::VulkanSpirV,
                                                    ShaderCompileProfile::VulkanES31, input.editor,
                                                    std::move(configurations), source.request, error))
            {
                return false;
            }
            deployment.policy = source.request.policy;
            ShaderDeploymentSource record;
            record.name = source.asset.name;
            record.source_hash = source.source_hash;
            std::set<Sha256Hash> keys;
            std::size_t source_upper = 0u;
            for (const auto& selection : source.request.configurations)
            {
                const auto resolved = resolve_shader_permutation(shader_material_domain(source.asset), selection);
                if (!resolved.succeeded())
                {
                    error = resolved.errors.front().message;
                    return false;
                }
                if (!keys.insert(resolved.permutation->key).second)
                {
                    continue;
                }
                std::vector<ShaderVariantSelection> values;
                for (const auto& item : resolved.permutation->selections)
                {
                    values.push_back({item.name, item.kind == ShaderPermutationValueKind::Boolean
                                                     ? (item.boolean_value ? "true" : "false")
                                                     : item.enum_value});
                }
                ShaderAsset expanded;
                std::vector<Diagnostic> diagnostics;
                if (!expand_standard_surface(source.asset, values, expanded, diagnostics))
                {
                    error = diagnostics.empty() ? "Standard expansion failed." : format_diagnostic(diagnostics.front());
                    return false;
                }
                const auto plan =
                    plan_shader_compilation(shader_compile_source(expanded), {selection}, source.request.policy);
                if (!plan.succeeded())
                {
                    error = plan.error;
                    return false;
                }
                std::size_t engine_upper = 1u;
                for (const auto& feature : expanded.features)
                {
                    if (feature.feature == ShaderEngineFeature::Shadows ||
                        feature.feature == ShaderEngineFeature::Environment)
                    {
                        engine_upper *= 2u;
                    }
                }
                const auto factories =
                    expanded.usage == ShaderUsage::Global
                        ? 1u
                        : (supports_vertex_factory(expanded.vertex_factory_support, VertexFactoryType::Local) ? 1u
                                                                                                              : 0u) +
                              (supports_vertex_factory(expanded.vertex_factory_support, VertexFactoryType::GPUSkin)
                                   ? 1u
                                   : 0u);
                source_upper += expanded.passes.size() * factories * engine_upper;
                if (source_upper > max_shader_compile_source_programs)
                {
                    error = "Shader Cook source exceeds 1024 declared Programs before filtering: " + source.asset.name;
                    return false;
                }
                deployment.required_programs += plan.required.size();
                if (deployment.required_programs > max_shader_compile_job_programs)
                {
                    error = "Shader Cook exceeds the complete 4096 required Program job budget.";
                    return false;
                }
            }
            record.configurations.assign(keys.begin(), keys.end());
            deployment.sources.push_back(std::move(record));
            required_sources.push_back(std::move(source));
        }
        for (const auto& extra : settings.additional_configurations)
        {
            if (!names.count(extra.first))
            {
                error = "Unknown extra Shader source: " + extra.first;
                return false;
            }
        }
        for (const auto& entry : catalog.value().entries)
        {
            if (entry.file.root_type != "toy3d.MaterialAssetData" &&
                entry.file.root_type != "toy3d.MaterialInstanceAssetData")
            {
                continue;
            }
            const AssetRef reference{entry.file.asset_id, {}, entry.file.root_type, AssetRefStrength::Strong};
            const auto hierarchy = read_material_hierarchy(types, files, catalog.value().index, reference);
            if (!hierarchy.succeeded())
            {
                error = hierarchy.status().message;
                return false;
            }
            const auto source = usages.find(hierarchy.value().root.shader_name);
            if (source == usages.end() || source->second != ShaderUsage::Material)
            {
                error =
                    "Saved Material requires an absent or non-Material Shader: " + hierarchy.value().root.shader_name;
                return false;
            }
        }
        if (!validate_shader_deployment(deployment, error))
        {
            return false;
        }
        if (!platform.create_directory(output).succeeded() || !platform.create_directory(work).succeeded())
        {
            error = "Unable to create immutable Shader Cook destinations.";
            return false;
        }
        for (std::size_t i = 0u; i < required_sources.size(); ++i)
        {
            const auto& source = required_sources[i];
            const PhysicalPath request(work.utf8() + "/request_" + std::to_string(i) + ".txt");
            if (!platform
                     .write_text_utf8(request, serialize_shader_source_compile_request(source.request),
                                      FileWriteMode::CreateNew)
                     .succeeded())
            {
                error = "Unable to write Shader Cook source request.";
                return false;
            }
            std::vector<std::string> arguments = {"--toolchain-root",
                                                  input.toolchain.utf8(),
                                                  "compile-vulkan",
                                                  source.path.utf8(),
                                                  source.virtual_path,
                                                  source.asset.passes.front().name,
                                                  output.utf8(),
                                                  work.utf8() + "/source_" + std::to_string(i),
                                                  "--request",
                                                  request.utf8(),
                                                  "--engine-include-root",
                                                  engine_include.utf8()};
            if (has_project_include)
            {
                arguments.push_back("--project-include-root");
                arguments.push_back(project_include.utf8());
            }
            ProcessRunOptions options;
            options.timeout_ms = 120000u;
            const auto compiled = processes.run(input.compiler, arguments, options);
            std::cout << compiled.output;
            if (!compiled.succeeded())
            {
                error = "Shader Cook failed for " + source.asset.name + ": " + compiled.message + compiled.output;
                return false;
            }
        }
        ShaderBuildSettings current;
        if (!read_shader_build_settings(platform, engine_settings, project_settings, current, error) ||
            sha256(serialize_shader_build_settings(current)) != settings_hash)
        {
            error = "Shader build settings changed during Cook.";
            return false;
        }
        const auto current_catalog = scan_asset_catalog(types, files, roots, false);
        if (!current_catalog.succeeded())
        {
            error = current_catalog.status().message;
            return false;
        }
        std::size_t verified_programs = 0u;
        for (const auto& source : required_sources)
        {
            const auto stat = platform.stat(source.path);
            if (!stat.succeeded() || stat.value().type != FileType::File ||
                stat.value().size > maximum_cook_source_bytes)
            {
                error = "Shader source changed type or exceeded its byte budget during Cook: " + source.virtual_path;
                return false;
            }
            const auto text = platform.read_text_utf8(source.path);
            if (!text.succeeded() || sha256(text.value()) != source.source_hash)
            {
                error = "Shader source changed during Cook: " + source.virtual_path;
                return false;
            }
            if (source.asset.usage == ShaderUsage::Material)
            {
                const auto gathered =
                    collect_material_shader_configurations(types, files, current_catalog.value(), source.asset.name);
                if (!gathered.succeeded() || gathered.value().descriptors != material_descriptors)
                {
                    error = "Material graph changed during Shader Cook.";
                    return false;
                }
            }
            std::vector<ShaderMapIndex> indices;
            if (!read_shader_map_indices(platform, output, source.asset.name, ShaderTarget::VulkanSpirV,
                                         ShaderCompileProfile::VulkanES31, indices, error))
            {
                return false;
            }
            const auto record = std::find_if(deployment.sources.begin(), deployment.sources.end(),
                                             [&](const ShaderDeploymentSource& item)
                                             {
                                                 return item.name == source.asset.name;
                                             });
            std::vector<Sha256Hash> keys;
            for (const auto& index : indices)
            {
                if (index.source_hash != source.source_hash ||
                    serialize_shader_source_compile_request({index.policy, {{}}}) !=
                        serialize_shader_source_compile_request({deployment.policy, {{}}}))
                {
                    error = "Cook source/policy differs from the planned deployment.";
                    return false;
                }
                keys.push_back(index.permutation_key);
                verified_programs += index.programs.size();
                for (const auto& program : index.programs)
                {
                    const auto entry = read_verified_shader_map_entry(platform, output, program.entry_key);
                    if (!entry.succeeded() || !shader_map_index_matches_entry(index, program, entry))
                    {
                        error = "Cook entry failed verification.";
                        return false;
                    }
                    for (const auto& stage : entry.entry->stages)
                    {
                        for (const auto& dependency : stage.request.dependencies)
                        {
                            const auto path = VirtualPath::parse(dependency.virtual_path);
                            const auto bytes = path.succeeded()
                                                   ? files.read_text_utf8(path.value(), maximum_cook_source_bytes)
                                                   : FileResult<std::string>(path.status());
                            if (!bytes.succeeded() || sha256(bytes.value()) != dependency.content_hash)
                            {
                                error = "Shader include changed during Cook: " + dependency.virtual_path;
                                return false;
                            }
                        }
                    }
                }
            }
            if (record == deployment.sources.end() || keys != record->configurations)
            {
                error = "Cook configuration coverage differs from the planned deployment.";
                return false;
            }
        }
        if (verified_programs != deployment.required_programs)
        {
            error = "Cook Program coverage differs from the planned deployment.";
            return false;
        }
        const auto written =
            platform.write_text_utf8(PhysicalPath(output.utf8() + "/deployment.txt"),
                                     serialize_shader_deployment(deployment), FileWriteMode::CreateNew);
        if (!written.succeeded())
        {
            error = written.message;
            return false;
        }
        std::cout << "Published Shader deployment: " << deployment.sources.size() << " sources, "
                  << deployment.required_programs << " required Programs.\n";
        return true;
    }
} // namespace toy3d::shader
