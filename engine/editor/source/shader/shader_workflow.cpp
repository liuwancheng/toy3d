#include "shader/shader_workflow.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <charconv>
#include <exception>
#include <set>
#include <sstream>
#include <utility>

#include "file_system/directory_file_store.h"
#include "shader/shader_map_entry.h"
#include "shader/shader_program_contract.h"
#include "rendercore/shader/shader_map_collection.h"
#include "shader_parameters/toy3d_editor_hitproxy.generated.h"
#include "shader_parameters/toy3d_shadowdepth_default.generated.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"
#include "logging/logger.h"
#include "asset/material/material_asset.h"
#include "workspace/editor_workspace.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/material/material_asset_builder.h"
#include "misc/utf8.h"

#include "platform/platform_defines.h"
#include "frontend/shader_parser.h"
#include "compiler/shader_source_discovery.h"
#include "compiler/variant_permutation.h"

namespace toy3d
{
    namespace
    {
        std::string comparable(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.size() > 1u && path.back() == '/')
            {
                path.pop_back();
            }
#if WITH_WIN
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
#endif
            return path;
        }
        bool contains(const PhysicalPath& root, const PhysicalPath& path)
        {
            const std::string prefix = comparable(root.utf8()) + "/";
            return comparable(path.utf8()).compare(0, prefix.size(), prefix) == 0;
        }
        bool safe_relative(const std::string& relative)
        {
            const auto parsed = VirtualPath::parse("/Source/" + relative);
            return !relative.empty() && relative.front() != '/' && relative.find('\\') == std::string::npos &&
                   relative.find(':') == std::string::npos && parsed.succeeded() &&
                   parsed.value().utf8() == "/Source/" + relative;
        }
    } // namespace

    // --------------------------------------------------------------------------
    // ShaderWorkflow: GT source registration, isolated compilation and publication
    // --------------------------------------------------------------------------
    ShaderWorkflow::~ShaderWorkflow()
    {
        shutdown();
    }

    void ShaderWorkflow::set_material_workspace(
        EditorWorkspace& workspace,
        std::function<std::vector<shader::ShaderPermutationSelection>(const std::string&)> draft)
    {
        material_workspace_ = &workspace;
        draft_configuration_ = std::move(draft);
    }

    bool ShaderWorkflow::validate_material_inputs(std::string& error)
    {
        if (!material_workspace_ || !material_snapshot_)
        {
            return true;
        }
        if (!material_workspace_->refresh())
        {
            error = material_workspace_->error();
            return false;
        }
        const auto gathered = collect_material_shader_configurations(
            material_workspace_->types(), material_workspace_->files(), material_workspace_->catalog(), request_name_);
        if (!gathered.succeeded())
        {
            error = gathered.status().message;
            return false;
        }
        if (gathered.value().descriptors != material_descriptors_)
        {
            error = "Material descriptors changed during compilation. Recompile the current saved configuration graph.";
            return false;
        }
        return true;
    }

    bool ShaderWorkflow::mount(const PhysicalPath& root, const std::string& name, bool writable, std::string& error)
    {
        DirectoryFileStoreDesc desc;
        desc.physical_root = root;
        desc.writable = writable;
        const auto store = DirectoryFileStore::create(platform_, desc);
        if (!store.succeeded())
        {
            error = store.status().message;
            return false;
        }
        const auto path = VirtualPath::parse(name);
        if (!path.succeeded())
        {
            error = path.status().message;
            return false;
        }
        FileMountDesc mounted;
        mounted.virtual_root = path.value();
        mounted.store = store.value();
        mounted.access = writable ? MountAccess::ReadWrite : MountAccess::ReadOnly;
        mounted.allow_enumeration = name == "/Project/Shaders";
        const auto added = files_.add_mount(mounted);
        if (!added.succeeded())
        {
            error = added.message;
            return false;
        }
        return true;
    }

    bool ShaderWorkflow::initialize(ShaderWorkflowPaths paths, MaterialRef defaults, std::string& error)
    {
        paths_ = std::move(paths);
        shader::ShaderBuildSettings settings;
        if (!shader::read_shader_build_settings(platform_, paths_.engine_build_settings, paths_.project_build_settings,
                                                settings, error))
        {
            return false;
        }
        defaults_ = std::move(defaults);
        if (!defaults_ || !defaults_->desc().shader_map)
        {
            error = "Default material Shader is unavailable.";
            return false;
        }
        const auto made = platform_.create_directories(paths_.saved);
        if (!made.succeeded())
        {
            error = made.message;
            return false;
        }
        if (!mount(paths_.engine_shader, "/Engine/Shaders", false, error) ||
            !mount(paths_.engine_include, "/Engine/ShaderIncludes", false, error) ||
            !mount(paths_.saved, "/Saved", true, error))
        {
            return false;
        }
        if (!paths_.project_shader.empty())
        {
            if (!mount(paths_.project_shader, "/Project/Shaders", true, error))
            {
                return false;
            }
            const auto include = platform_.join_relative(paths_.project_shader, "include");
            if (!include.succeeded())
            {
                error = include.status().message;
                return false;
            }
            const auto exists = platform_.exists(include.value());
            if (!exists.succeeded())
            {
                error = exists.status().message;
                return false;
            }
            if (exists.value() && !mount(include.value(), "/Project/ShaderIncludes", false, error))
            {
                return false;
            }
        }
        const auto frozen = files_.freeze();
        if (!frozen.succeeded())
        {
            error = frozen.message;
            return false;
        }
        if (!read_sources(error))
        {
            return false;
        }
        const auto global_path = VirtualPath::parse("/Saved/globals.txt");
        const auto global_text = files_.read_text_utf8(global_path.value(), maximum_shader_manifest_bytes);
        if (global_text.succeeded())
        {
            std::istringstream records(global_text.value());
            std::string header, line;
            bool valid = std::getline(records, header) && header == "Toy3dGlobalShaders 1";
            while (valid && std::getline(records, line))
            {
                const auto first = line.find('\t'), last = line.rfind('\t');
                if (first == std::string::npos || first == last)
                {
                    valid = false;
                    break;
                }
                const std::string name = line.substr(0, first);
                const auto* source = find(name);
                const std::string relative = line.substr(first + 1u, last - first - 1u);
                // optional represents a malformed digest without accepting zero.
                const auto hash = sha256_from_hex(line.substr(last + 1u));
                if (!source || source->usage != BuiltinShaderUsage::Global || !hash || !safe_relative(relative) ||
                    !global_restore_.emplace(name, std::make_pair(relative, *hash)).second)
                {
                    valid = false;
                }
            }
            std::size_t required = 0u;
            for (const auto& source : sources_)
            {
                if (source.usage == BuiltinShaderUsage::Global)
                {
                    ++required;
                }
            }
            if (!valid || global_restore_.size() != required)
            {
                global_restore_.clear();
                TOY_LOG_WARN("Saved Global Shader record is invalid; validating deployed versions.");
            }
        }
        else if (global_text.status().code != FileErrorCode::NotFound)
        {
            TOY_LOG_WARN("Cannot read Saved Global Shader record: {}. Validating deployed versions.",
                         global_text.status().message);
        }
        for (const auto& source : sources_)
        {
            const auto pointer = VirtualPath::parse("/Saved/" + sha256_to_hex(sha256(source.name)) + "/current.txt");
            const auto existing = files_.stat(pointer.value());
            if (!source.artifacts.empty() || existing.succeeded())
            {
                restore_queue_.push_back(source.name);
            }
            else if (existing.status().code != FileErrorCode::NotFound)
            {
                TOY_LOG_WARN("Cannot inspect Saved Shader [{}]: {}", source.name, existing.status().message);
            }
        }
        begin_task(restore_queue_.size(), true);
        status_ = "Ready. Save source in VS Code, then Recompile.";
        return true;
    }

    bool ShaderWorkflow::read_sources(std::string& error)
    {
        std::vector<EditorShaderSource> values;
        for (const auto& builtin : builtin_shader_sources)
        {
            const auto path = VirtualPath::parse(std::string("/Engine/Shaders/") + builtin.source);
            const auto artifacts = platform_.join_relative(paths_.builtin_root, builtin.directory);
            if (!path.succeeded() || !artifacts.succeeded())
            {
                error = "Cannot resolve builtin Shader registration.";
                return false;
            }
            EditorShaderSource source;
            source.name = builtin.name;
            source.path = path.value();
            source.pass = builtin.pass;
            source.usage = builtin.usage;
            source.artifacts = artifacts.value();
            values.push_back(std::move(source));
        }
        if (!paths_.project_shader.empty())
        {
            const auto discovered =
                shader::discover_shader_sources(files_, VirtualPath::parse("/Project/Shaders").value());
            if (!discovered.succeeded())
            {
                error = discovered.status().message;
                return false;
            }
            std::set<std::string> names;
            for (const auto& source : values)
            {
                names.insert(source.name);
            }
            for (const auto& item : discovered.value())
            {
                EditorShaderSource source;
                source.path = item.path;
                source.name = item.name;
                if (item.pass_names.size() == 1u)
                {
                    source.pass = item.pass_names.front();
                }
                source.discovery_error = item.error;
                source.name_conflict = item.name_conflict;
                // Material admission follows roles and declared factories, independently
                // of authored Pass display names or a preferred primary factory.
                const auto forward =
                    std::find(item.pass_roles.begin(), item.pass_roles.end(), shader::ShaderPassRole::Forward);
                if (source.discovery_error.empty() &&
                    (source.name.compare(0u, 16u, "Project/Surface/") != 0 ||
                     item.usage != shader::ShaderUsage::Material || forward == item.pass_roles.end()))
                {
                    source.discovery_error =
                        "Project sources require Usage Material, a Project/Surface/ name and a Forward role.";
                }
                if (forward != item.pass_roles.end())
                {
                    source.pass = item.pass_names[static_cast<std::size_t>(forward - item.pass_roles.begin())];
                }
                if (source.name.empty())
                {
                    const auto old = std::find_if(sources_.begin(), sources_.end(),
                                                  [&](const EditorShaderSource& previous)
                                                  {
                                                      return previous.path == item.path;
                                                  });
                    source.name = old == sources_.end() ? item.path.utf8() : old->name;
                }
                if (!names.insert(source.name).second)
                {
                    source.name_conflict = true;
                    source.discovery_error =
                        "Duplicate or reserved Shader name: " + source.name + " at " + item.path.utf8();
                    source.name = item.path.utf8();
                }
                if (!source.discovery_error.empty())
                {
                    source.diagnostic = source.discovery_error;
                }
                values.push_back(std::move(source));
            }
        }
        for (auto& source : values)
        {
            const auto old = find(source.name);
            if (old && !source.name_conflict)
            {
                // A malformed edit does not invalidate a previously published
                // Program. Conflicting/deleted identities cannot revive it.
                source.shader_map = old->shader_map;
                source.configurations = old->configurations;
                source.properties = old->properties;
                if (source.discovery_error.empty())
                {
                    source.diagnostic = old->diagnostic;
                }
            }
        }
        sources_ = std::move(values);
        return true;
    }

    const EditorShaderSource* ShaderWorkflow::find(const std::string& name) const
    {
        for (const auto& source : sources_)
        {
            if (source.name == name)
            {
                return &source;
            }
        }
        return nullptr;
    }
    ShaderMapCollectionRef ShaderWorkflow::shader_map(
        const std::string& name, const std::vector<shader::ShaderPermutationSelection>& selections) const
    {
        const auto* source = find(name);
        if (!source || !source->shader_map)
        {
            return {};
        }
        const auto configuration =
            shader::resolve_shader_permutation(source->shader_map->index().material_domain, selections);
        if (!configuration.succeeded())
        {
            return {};
        }
        const auto found = source->configurations.find(configuration.permutation->key);
        return found == source->configurations.end() ? nullptr : found->second;
    }

    bool ShaderWorkflow::physical_source(const EditorShaderSource& source, PhysicalPath& output,
                                         std::string& error) const
    {
        const std::string engine_prefix = "/Engine/Shaders/";
        const bool engine = source.path.utf8().compare(0, engine_prefix.size(), engine_prefix) == 0;
        const PhysicalPath& root = engine ? paths_.engine_shader : paths_.project_shader;
        const std::string prefix = engine ? "/Engine/Shaders/" : "/Project/Shaders/";
        if (source.path.utf8().compare(0, prefix.size(), prefix) != 0)
        {
            error = "Shader source has no allowed root.";
            return false;
        }
        const auto joined = platform_.join_relative(root, source.path.utf8().substr(prefix.size()));
        if (!joined.succeeded())
        {
            error = joined.status().message;
            return false;
        }
        const auto canonical_root = platform_.canonical(root);
        const auto canonical_file = platform_.canonical(joined.value());
        if (!canonical_root.succeeded() || !canonical_file.succeeded() ||
            !contains(canonical_root.value(), canonical_file.value()))
        {
            error = "Shader source is missing or escapes its registered root.";
            return false;
        }
        const auto checked = files_.read_text_utf8(source.path, maximum_shader_source_bytes);
        if (!checked.succeeded())
        {
            error = checked.status().message;
            return false;
        }
        output = canonical_file.value();
        return true;
    }

    bool ShaderWorkflow::request_failed(const std::string& operation, const std::string& name)
    {
        if (operation == "Recompile")
        {
            for (auto& source : sources_)
            {
                if (source.name == name)
                {
                    source.diagnostic = error_;
                }
            }
            record_task_error(name, error_);
        }
        TOY_LOG_ERROR("Shader {} [{}]: {}", operation, name, error_);
        return false;
    }

    bool ShaderWorkflow::open_source(const std::string& name, std::uint32_t line, std::uint32_t column)
    {
        const auto* source = find(name);
        PhysicalPath physical;
        if (!source || !physical_source(*source, physical, error_))
        {
            if (!source)
            {
                error_ = "Shader source is not registered: " + name;
            }
            return request_failed("Open source", name);
        }
        PhysicalPath executable = paths_.code_executable;
        if (executable.empty())
        {
            std::vector<std::string> candidates;
#if WITH_WIN
            if (const char* local = std::getenv("LOCALAPPDATA"))
            {
                candidates.push_back(std::string(local) + "/Programs/Microsoft VS Code/Code.exe");
            }
            if (const char* profile = std::getenv("USERPROFILE"))
            {
                candidates.push_back(std::string(profile) + "/Program Files/Microsoft VS Code/Code.exe");
            }
            if (const char* installed = std::getenv("ProgramFiles"))
            {
                candidates.push_back(std::string(installed) + "/Microsoft VS Code/Code.exe");
            }
#elif WITH_MAC
            candidates.push_back("/Applications/Visual Studio Code.app/Contents/MacOS/Electron");
#else
            candidates.push_back("/usr/share/code/code");
            candidates.push_back("/usr/bin/code");
#endif
            for (const auto& candidate : candidates)
            {
                const auto exists = platform_.exists(PhysicalPath(candidate));
                if (!exists.succeeded())
                {
                    error_ = exists.status().message;
                    return request_failed("Open source", name);
                }
                if (exists.value())
                {
                    executable = PhysicalPath(candidate);
                    break;
                }
            }
        }
        if (executable.empty())
        {
            error_ = "VS Code was not found. Start Editor with --Editor.CodeExecutable=<absolute Code.exe path>.";
            return request_failed("Open source", name);
        }
        const auto opened =
            processes_.launch_detached(executable, {"--reuse-window", "--goto",
                                                    physical.utf8() + ":" + std::to_string(std::max(line, 1u)) + ":" +
                                                        std::to_string(std::max(column, 1u))});
        if (!opened.succeeded())
        {
            error_ = opened.message;
            return request_failed("Open source", name);
        }
        error_.clear();
        status_ = "Opened source in VS Code: " + physical.utf8();
        return true;
    }

    bool ShaderWorkflow::start_compile(const std::string& name, AssetId origin, std::uint64_t session_revision)
    {
        restoring_ = false;
        request_name_ = name;
        error_.clear();
        output_.clear();
        task_.current_source = name;
        task_.phase = ShaderTaskPhase::Compiling;
        const auto* source = find(name);
        if (source && !source->discovery_error.empty())
        {
            error_ = source->discovery_error;
            output_ = error_;
            return request_failed("Recompile", name);
        }
        PhysicalPath physical;
        if (!source || !physical_source(*source, physical, error_))
        {
            if (!source)
            {
                error_ = "Shader is not registered.";
            }
            return request_failed("Recompile", name);
        }
        const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!text.succeeded())
        {
            error_ = text.status().message;
            return request_failed("Recompile", name);
        }
        if (!AssetId::try_generate(request_id_))
        {
            error_ = "Unable to allocate Shader compile request identity.";
            return request_failed("Recompile", name);
        }
        request_name_ = name;
        origin_ = origin;
        origin_revision_ = session_revision;
        source_hash_ = sha256(text.value());
        candidate_relative_ = "requests/" + request_id_.hex();
        const auto directory = platform_.join_relative(paths_.saved, candidate_relative_);
        if (!directory.succeeded())
        {
            error_ = directory.status().message;
            return request_failed("Recompile", name);
        }
        request_directory_ = directory.value();
        const auto made = platform_.create_directories(request_directory_);
        if (!made.succeeded())
        {
            error_ = made.message;
            return request_failed("Recompile", name);
        }
        const auto output = platform_.join_relative(request_directory_, "entries");
        const auto work = platform_.join_relative(request_directory_, "work");
        if (!output.succeeded() || !work.succeeded())
        {
            error_ = "Unable to resolve Shader compile output paths.";
            return request_failed("Recompile", name);
        }
        shader::ShaderSourceCompileRequest compile_request;
        material_descriptors_.clear();
        material_snapshot_ = false;
        if (source->usage == BuiltinShaderUsage::Material && material_workspace_)
        {
            if (!material_workspace_->refresh())
            {
                error_ = material_workspace_->error();
                return request_failed("Gather Material configurations", name);
            }
            const auto gathered = collect_material_shader_configurations(
                material_workspace_->types(), material_workspace_->files(), material_workspace_->catalog(), name);
            if (!gathered.succeeded())
            {
                error_ = gathered.status().message;
                return request_failed("Gather Material configurations", name);
            }
            compile_request.configurations = gathered.value().configurations;
            material_descriptors_ = gathered.value().descriptors;
            material_snapshot_ = true;
        }
        if (source->usage == BuiltinShaderUsage::Material && draft_configuration_)
        {
            compile_request.configurations.push_back(draft_configuration_(name));
        }
        shader::ShaderBuildSettings settings;
        if (!shader::read_shader_build_settings(platform_, paths_.engine_build_settings, paths_.project_build_settings,
                                                settings, error_) ||
            !shader::make_shader_source_compile_request(
                settings, name, shader::ShaderTarget::VulkanSpirV, shader::ShaderCompileProfile::VulkanES31, true,
                std::move(compile_request.configurations), compile_request, error_))
        {
            return request_failed("Gather Shader build policy", name);
        }
        build_settings_hash_ = sha256(shader::serialize_shader_build_settings(settings));
        requested_configurations_ = compile_request.configurations;
        const auto request_text = shader::serialize_shader_source_compile_request(compile_request);
        const auto request_path = platform_.join_relative(request_directory_, "compile_request.txt");
        if (request_text.empty() || !request_path.succeeded())
        {
            error_ = "Invalid or oversized Shader source compile request.";
            return request_failed("Gather Material configurations", name);
        }
        const auto request_written =
            platform_.write_text_utf8(request_path.value(), request_text, FileWriteMode::CreateNew);
        if (!request_written.succeeded())
        {
            error_ = request_written.message;
            return request_failed("Write compile request", name);
        }
        std::vector<std::string> arguments = {"--toolchain-root",
                                              paths_.toolchain.utf8(),
                                              "compile-vulkan",
                                              physical.utf8(),
                                              source->path.utf8(),
                                              source->pass,
                                              output.value().utf8(),
                                              work.value().utf8(),
                                              "--request",
                                              request_path.value().utf8(),
                                              "--engine-include-root",
                                              paths_.engine_include.utf8()};
        const auto project_include = platform_.join_relative(paths_.project_shader, "include");
        if (!project_include.succeeded())
        {
            error_ = project_include.status().message;
            return request_failed("Recompile", name);
        }
        const auto includes_exist = platform_.exists(project_include.value());
        if (!includes_exist.succeeded())
        {
            error_ = includes_exist.status().message;
            return request_failed("Recompile", name);
        }
        if (includes_exist.value())
        {
            arguments.push_back("--project-include-root");
            arguments.push_back(project_include.value().utf8());
        }
        cancel_.store(false);
        result_ = std::make_shared<CompileResult>();
        try
        {
            worker_ = std::make_unique<Thread>(threads_, "ShaderCompiler",
                                               [this, result = result_, arguments = std::move(arguments)]()
                                               {
                                                   try
                                                   {
                                                       ProcessRunOptions options;
                                                       options.timeout_ms = 120000u;
                                                       options.cancel = &cancel_;
                                                       result->process =
                                                           processes_.run(paths_.compiler, arguments, options);
                                                   }
                                                   catch (const std::exception& error)
                                                   {
                                                       result->process.error = ProcessError::Launch;
                                                       result->process.message = error.what();
                                                   }
                                                   result->complete.store(true, std::memory_order_release);
                                               });
        }
        catch (const std::exception& error)
        {
            result_.reset();
            error_ = error.what();
            return request_failed("Recompile", name);
        }
        error_.clear();
        output_.clear();
        status_ = "Compiling " + name + " (saved files)...";
        return true;
    }

    bool ShaderWorkflow::error_location(std::uint32_t& line, std::uint32_t& column) const
    {
        const auto* source = find(request_name_);
        if (!source || error_.empty())
        {
            return false;
        }
        const std::string prefix = source->path.utf8() + ":";
        // Prefer DXC's concrete error over the compiler's outer failure message.
        std::istringstream input(output_);
        std::string message;
        bool found = false;
        while (std::getline(input, message))
        {
            const auto offset = message.find(prefix);
            if (offset == std::string::npos)
            {
                continue;
            }
            const char* begin = message.data() + offset + prefix.size();
            const char* end = message.data() + message.size();
            std::uint32_t source_line = 0u, source_column = 0u;
            // from_chars reads bounded compiler text without locale or exceptions.
            const auto parsed_line = std::from_chars(begin, end, source_line);
            if (parsed_line.ec != std::errc{} || parsed_line.ptr == end || *parsed_line.ptr != ':')
            {
                continue;
            }
            const auto parsed_column = std::from_chars(parsed_line.ptr + 1, end, source_column);
            if (parsed_column.ec != std::errc{} || !source_line || !source_column)
            {
                continue;
            }
            line = source_line;
            column = source_column;
            found = true;
            if (message.find(": error:", offset) != std::string::npos)
            {
                return true;
            }
        }
        return found;
    }

    bool ShaderWorkflow::has_error_location() const
    {
        std::uint32_t line = 0u, column = 0u;
        return error_location(line, column);
    }
    bool ShaderWorkflow::open_error()
    {
        std::uint32_t line = 0u, column = 0u;
        if (!error_location(line, column))
        {
            error_ = "No registered source location was found in compiler output.";
            return request_failed("Open diagnostic", request_name_);
        }
        return open_source(request_name_, line, column);
    }

    bool ShaderWorkflow::validate_interface(const ShaderMapCollection& candidate, std::string& error) const
    {
        for (const auto& program : candidate.programs())
        {
            const auto& data = program->data();
            if (data.contract.usage != shader::ShaderUsage::Material || data.platform != ShaderPlatform::VulkanES31)
            {
                error = "Only the current Vulkan ES3.1 Material compile target is available.";
                return false;
            }
            // Full engine ABI is authoritative. A default Material's reflection may omit
            // resources (notably Sky Cube) that another valid source actively consumes.
            const ViewShaderParameters view;
            const ObjectShaderParameters object;
            const GPUSkinObjectShaderParameters skin_object;
            const ForwardPassParameters forward;
            const ShadowDepthPassParameters shadow;
            const HitProxyPassParameters hit;
            const auto& object_metadata = data.contract.vertex_factory == shader::VertexFactoryType::GPUSkin
                                              ? shader_parameters_metadata(skin_object)
                                              : shader_parameters_metadata(object);
            const auto* pass_metadata = &shader_parameters_metadata(forward);
            if (data.contract.role == shader::ShaderPassRole::ShadowDepth)
            {
                pass_metadata = &shader_parameters_metadata(shadow);
            }
            else if (data.contract.role == shader::ShaderPassRole::HitProxy)
            {
                pass_metadata = &shader_parameters_metadata(hit);
            }
            const ShaderParametersMetadata* groups[] = {&shader_parameters_metadata(view), &object_metadata,
                                                        pass_metadata};
            for (const auto* metadata : groups)
            {
                const auto status = validate_shader_parameters_group_against_schema(*metadata, data.parameter_schema);
                if (!status)
                {
                    error = "Shader changed an engine-owned group ABI: " + status.message();
                    return false;
                }
                const auto active = shader_parameters_metadata_for_program(*metadata, data);
                if (!active)
                {
                    error =
                        "Material active engine binding does not match its canonical ABI: " + active.status().message();
                    return false;
                }
            }
            for (const auto& buffer : data.parameter_schema.constant_buffers)
            {
                if (buffer.group == shader::BindingGroup::Global)
                {
                    error = "Material cannot declare engine Global constants.";
                    return false;
                }
            }
            for (const auto& resource : data.parameter_schema.resources)
            {
                if (resource.group == shader::BindingGroup::Global)
                {
                    error = "Material cannot declare engine Global resources.";
                    return false;
                }
            }
        }
        return true;
    }

    bool ShaderWorkflow::load_candidate(const PhysicalPath& directory, const std::string& name, std::string& error)
    {
        const auto canonical = platform_.canonical(directory);
        const auto saved = platform_.canonical(paths_.saved);
        const auto builtin = platform_.canonical(paths_.builtin_root);
        if (!canonical.succeeded() || !((saved.succeeded() && contains(saved.value(), canonical.value())) ||
                                        (builtin.succeeded() && contains(builtin.value(), canonical.value()))))
        {
            error = "Shader artifact directory is unavailable or escapes its configured root: " + directory.utf8();
            return false;
        }
        const auto* source = find(name);
        if (!source)
        {
            error = "Shader source is not registered.";
            return false;
        }
        const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!text.succeeded())
        {
            error = text.status().message;
            return false;
        }
        const auto parsed = shader::parse_shader(text.value(), source->path.utf8());
        if (!parsed.succeeded() || parsed.asset->name != name)
        {
            error = "Cannot parse matching registered Shader declaration.";
            for (const auto& diagnostic : parsed.diagnostics)
            {
                error += "\n" + shader::format_diagnostic(diagnostic);
            }
            return false;
        }
        const auto domain = shader::shader_material_domain(*parsed.asset);
        const auto defaults = shader::resolve_shader_permutation(domain, {});
        if (!defaults.succeeded())
        {
            error = "Cannot resolve Shader default configuration.";
            return false;
        }
        std::vector<shader::ShaderMapIndex> indices;
        if (!shader::read_shader_map_indices(platform_, canonical.value(), name, shader::ShaderTarget::VulkanSpirV,
                                             shader::ShaderCompileProfile::VulkanES31, indices, error))
        {
            return false;
        }
        if (indices.front().source_hash != sha256(text.value()) ||
            shader::serialize_shader_permutation_domain(indices.front().material_domain) !=
                shader::serialize_shader_permutation_domain(domain) ||
            !indices.front().policy.editor)
        {
            error = "Shader artifact source/domain/Editor policy does not match the current request. Recompile: " +
                    source->path.utf8();
            return false;
        }
        std::vector<std::vector<shader::ShaderPermutationSelection>> requested = requested_configurations_;
        if (restoring_ && source->usage == BuiltinShaderUsage::Material && material_workspace_)
        {
            const auto gathered = collect_material_shader_configurations(
                material_workspace_->types(), material_workspace_->files(), material_workspace_->catalog(), name);
            if (!gathered.succeeded())
            {
                error = gathered.status().message;
                return false;
            }
            requested = gathered.value().configurations;
        }
        shader::ShaderBuildSettings settings;
        shader::ShaderSourceCompileRequest expected_request;
        if (!shader::read_shader_build_settings(platform_, paths_.engine_build_settings, paths_.project_build_settings,
                                                settings, error) ||
            !shader::make_shader_source_compile_request(settings, name, shader::ShaderTarget::VulkanSpirV,
                                                        shader::ShaderCompileProfile::VulkanES31, true,
                                                        std::move(requested), expected_request, error))
        {
            return false;
        }
        const auto settings_hash = sha256(shader::serialize_shader_build_settings(settings));
        if ((!restoring_ && settings_hash != build_settings_hash_) ||
            shader::serialize_shader_source_compile_request({expected_request.policy, {{}}}) !=
                shader::serialize_shader_source_compile_request({indices.front().policy, {{}}}))
        {
            error = "Shader artifact family policy differs from the current build settings.";
            return false;
        }
        build_settings_hash_ = settings_hash;
        requested = std::move(expected_request.configurations);
        requested.push_back({});
        for (const auto& selection : requested)
        {
            const auto resolved = shader::resolve_shader_permutation(domain, selection);
            if (!resolved.succeeded())
            {
                error = resolved.errors.front().message;
                return false;
            }
            if (std::none_of(indices.begin(), indices.end(),
                             [&](const shader::ShaderMapIndex& index)
                             {
                                 return index.permutation_key == resolved.permutation->key;
                             }))
            {
                error = "Shader artifact family is missing requested configuration " +
                        sha256_to_hex(resolved.permutation->key);
                return false;
            }
        }
        ShaderMapEntryLoader loader(canonical.value());
        std::vector<ShaderMapCollectionRef> configurations;
        ShaderMapCollectionRef default_map;
        std::vector<shader::ShaderEditorProperty> properties;
        std::map<std::string, Sha256Hash> dependencies;
        Sha256Hash material_schema{};
        const auto factory =
            shader::supports_vertex_factory(parsed.asset->vertex_factory_support, shader::VertexFactoryType::Local)
                ? shader::VertexFactoryType::Local
                : shader::VertexFactoryType::GPUSkin;
        for (const auto& index : indices)
        {
            auto candidate = ShaderMapCollection::create_candidate(
                loader.load_collection(name, ShaderPlatform::VulkanES31, index.permutation_key));
            if (!candidate.succeeded())
            {
                error = candidate.error;
                return false;
            }
            if (source->usage == BuiltinShaderUsage::Material)
            {
                if (!validate_interface(*candidate.collection, error))
                {
                    return false;
                }
                const auto schema = material_parameter_schema_from_shader_schema(
                    candidate.collection->programs().front()->data().parameter_schema);
                if (!configurations.empty() && material_schema != schema.schema_identity)
                {
                    error = "Shader configurations disagree on complete Material schema.";
                    return false;
                }
                material_schema = schema.schema_identity;
                MaterialTextureValues textures;
                for (const auto& resource : defaults_->parameter_schema().resources)
                {
                    const auto found = defaults_->desc().texture_defaults.find(resource.parameter_id);
                    if (found != defaults_->desc().texture_defaults.end())
                    {
                        textures.named_defaults[resource.default_value] = found->second;
                    }
                }
                if (material_workspace_)
                {
                    const auto defaults = resolve_builtin_material_texture_defaults(
                        material_workspace_->files(), material_workspace_->catalog().index, schema, textures);
                    if (!defaults.succeeded())
                    {
                        error = defaults.message;
                        return false;
                    }
                }
                MaterialAssetData descriptor;
                descriptor.shader_name = name;
                descriptor.static_options = material_static_options(index.material_selections);
                MaterialInstanceRef checked;
                {
                    const auto built = create_material_from_asset(descriptor, candidate.collection, textures);
                    if (!built.succeeded())
                    {
                        error = built.status().message;
                        return false;
                    }
                    checked = built.value();
                }
                MaterialInstance::release(checked);
            }
            for (const auto& record : index.programs)
            {
                const auto verified =
                    shader::read_verified_shader_map_entry(platform_, canonical.value(), record.entry_key);
                if (!shader::shader_map_index_matches_entry(index, record, verified))
                {
                    error = "Cannot verify indexed ShaderMapEntry " + sha256_to_hex(record.entry_key);
                    return false;
                }
                for (const auto& stage : verified.entry->stages)
                {
                    for (const auto& dependency : stage.request.dependencies)
                    {
                        if (dependency.virtual_path.compare(0, 11u, "/Generated/") == 0 ||
                            dependency.virtual_path.compare(0, 10u, "builtin://") == 0)
                        {
                            continue;
                        }
                        const auto path = VirtualPath::parse(dependency.virtual_path);
                        if (!path.succeeded())
                        {
                            error = path.status().message;
                            return false;
                        }
                        const auto current = files_.read_text_utf8(path.value(), maximum_shader_source_bytes);
                        if (!current.succeeded() || sha256(current.value()) != dependency.content_hash)
                        {
                            error =
                                "Shader dependency changed or is unavailable. Recompile: " + dependency.virtual_path;
                            return false;
                        }
                        const auto previous = dependencies.find(dependency.virtual_path);
                        if (previous != dependencies.end() && previous->second != dependency.content_hash)
                        {
                            error = "Shader configurations disagree on source dependency revisions.";
                            return false;
                        }
                        dependencies[dependency.virtual_path] = dependency.content_hash;
                    }
                }
                if (source->usage == BuiltinShaderUsage::Material &&
                    index.permutation_key == defaults.permutation->key &&
                    verified.entry->contract.vertex_factory == factory &&
                    verified.entry->contract.role == shader::ShaderPassRole::Forward &&
                    !shader::read_shader_editor_properties(
                        platform_, *verified.entry_directory, name,
                        candidate.collection->find(shader::ShaderPassRole::Forward, factory)
                            .program->data()
                            .parameter_schema,
                        properties, error))
                {
                    return false;
                }
            }
            if (index.permutation_key == defaults.permutation->key)
            {
                default_map = candidate.collection;
            }
            configurations.push_back(std::move(candidate.collection));
        }
        if (!default_map || !validate_material_inputs(error))
        {
            if (!default_map)
            {
                error = "Shader configuration family has no typed default configuration.";
            }
            return false;
        }
        candidate_ = std::move(default_map);
        candidate_configurations_ = std::move(configurations);
        validated_material_targets_.clear();
        material_targets_captured_ = false;
        completed_material_validations_.clear();
        candidate_properties_ = std::move(properties);
        candidate_dependencies_ = std::move(dependencies);
        validation_configuration_ = 0u;
        if (source->usage == BuiltinShaderUsage::Material)
        {
            validation_ = std::make_shared<MaterialShaderMapValidation>();
            validation_->shader_map = candidate_configurations_.front();
        }
        validation_sent_ = false;
        validation_attempts_ = 0u;
        return true;
    }

    void ShaderWorkflow::tick()
    {
        if (!busy() && task_.phase != ShaderTaskPhase::Idle && task_.phase != ShaderTaskPhase::Completed)
        {
            complete_task();
        }
        if (builtin_update_)
        {
            if (builtin_update_->prepared.load(std::memory_order_acquire) &&
                builtin_update_->decision.load() == BuiltinShaderDecision::Pending)
            {
                if (!builtin_update_->status.succeeded())
                {
                    error_ = builtin_update_->status.message();
                    bool saved = false;
                    for (const auto& revision : builtin_revisions_)
                    {
                        saved = saved || revision.relative.compare(0u, 9u, "requests/") == 0;
                    }
                    if (restoring_ && saved)
                    {
                        std::vector<std::string> fallback;
                        for (const auto& revision : builtin_revisions_)
                        {
                            fallback.push_back(revision.name);
                            global_restore_.erase(revision.name);
                            ignore_saved_.insert(revision.name);
                        }
                        restore_queue_.insert(restore_queue_.begin(), fallback.begin(), fallback.end());
                        TOY_LOG_WARN("Saved builtin Shader group failed validation: {}. Validating deployed group.",
                                     error_);
                        builtin_update_.reset();
                        builtin_revisions_.clear();
                        error_.clear();
                        return;
                    }
                    for (auto& source : sources_)
                    {
                        for (const auto& revision : builtin_revisions_)
                        {
                            if (source.name == revision.name)
                            {
                                source.diagnostic = error_;
                            }
                        }
                    }
                    status_ = "Builtin Shader validation failed; previous programs remain active.";
                    for (const auto& revision : builtin_revisions_)
                    {
                        record_task_error(revision.name, error_);
                    }
                    TOY_LOG_ERROR("Builtin Shader validation failed: {}", error_);
                    finish_batch_item(false, builtin_revisions_.size());
                    builtin_update_.reset();
                    builtin_revisions_.clear();
                    return;
                }
                if (batch_cancelled_ || !publish_builtin())
                {
                    if (!batch_cancelled_)
                    {
                        for (const auto& revision : builtin_revisions_)
                        {
                            record_task_error(revision.name, error_);
                        }
                        TOY_LOG_ERROR("Builtin Shader publication failed: {}", error_);
                    }
                    builtin_update_->decision.store(BuiltinShaderDecision::Discard, std::memory_order_release);
                }
                else
                {
                    task_.phase = ShaderTaskPhase::Publishing;
                    builtin_update_->decision.store(BuiltinShaderDecision::Commit, std::memory_order_release);
                }
            }
            if (builtin_update_->resolved.load(std::memory_order_acquire))
            {
                const bool applied = builtin_update_->applied;
                if (applied)
                {
                    for (auto& revision : builtin_revisions_)
                    {
                        for (auto& source : sources_)
                        {
                            if (source.name == revision.name)
                            {
                                source.shader_map = revision.shader_map;
                                source.configurations.clear();
                                for (const auto& configuration : revision.configurations)
                                {
                                    source.configurations.emplace(configuration->index().permutation_key,
                                                                  configuration);
                                }
                                source.diagnostic.clear();
                            }
                        }
                    }
                    status_ = "Applied builtin Shaders.";
                    TOY_LOG_INFO("Applied {} builtin Shader revisions.", builtin_revisions_.size());
                }
                finish_batch_item(applied, builtin_revisions_.size());
                builtin_update_.reset();
                builtin_revisions_.clear();
                if (!busy())
                {
                    complete_task();
                }
            }
            return;
        }
        if (!active())
        {
            const auto& queue = batch_active_ ? compile_queue_ : restore_queue_;
            const std::size_t index = batch_active_ ? compile_index_ : 0u;
            const auto* next = index < queue.size() ? find(queue[index]) : nullptr;
            if ((!global_candidates_.empty() || global_failed_) && (!next || next->usage != BuiltinShaderUsage::Global))
            {
                finish_global_group();
                return;
            }
            if (batch_active_)
            {
                if (compile_index_ < compile_queue_.size() && !batch_cancelled_)
                {
                    const auto name = compile_queue_[compile_index_++];
                    if (!start_compile(name, {}, 0u))
                    {
                        if (find(name)->usage == BuiltinShaderUsage::Global)
                        {
                            global_failed_ = true;
                        }
                        else
                        {
                            finish_batch_item(false);
                        }
                    }
                }
                else
                {
                    finish_batch();
                    return;
                }
            }
            else if (!restore_queue_.empty())
            {
                const auto name = restore_queue_.front();
                restore_queue_.erase(restore_queue_.begin());
                restore(name);
            }
        }
        if (worker_ && result_->complete.load(std::memory_order_acquire))
        {
            worker_->join();
            worker_.reset();
            output_ = result_->process.output;
            if (batch_cancelled_)
            {
                result_.reset();
                reject("Shader compilation cancelled.");
                return;
            }
            if (!result_->process.succeeded())
            {
                reject(result_->process.message + "\n" + output_);
                result_.reset();
                return;
            }
            // Successful compilation may still emit warnings. Forward its diagnostics
            // once; failed processes already include the full output in reject().
            std::istringstream diagnostics(output_);
            std::string diagnostic;
            while (std::getline(diagnostics, diagnostic))
            {
                if (diagnostic.empty())
                {
                    continue;
                }
                if (diagnostic.find("[error]") != std::string::npos || diagnostic.find(": error:") != std::string::npos)
                {
                    TOY_LOG_ERROR("Shader compiler [{}]: {}", request_name_, diagnostic);
                }
                else if (diagnostic.find("[warning]") != std::string::npos ||
                         diagnostic.find(": warning:") != std::string::npos)
                {
                    TOY_LOG_WARN("Shader compiler [{}]: {}", request_name_, diagnostic);
                }
                else
                {
                    TOY_LOG_INFO("Shader compiler [{}]: {}", request_name_, diagnostic);
                }
            }
            result_.reset();
            const auto* source = find(request_name_);
            if (!source)
            {
                reject("Shader source registration changed during compilation.");
                return;
            }
            const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
            if (!text.succeeded() || sha256(text.value()) != source_hash_)
            {
                reject("Source changed during compilation. Save it and recompile.");
                return;
            }
            const auto entries = platform_.join_relative(request_directory_, "entries");
            std::string error;
            if (!entries.succeeded() || !load_candidate(entries.value(), request_name_, error))
            {
                reject(error);
                return;
            }
            task_.phase = ShaderTaskPhase::Validating;
            status_ = "Validating Shader pipeline on Rendering Thread...";
        }
        if (validation_ && validation_->complete.load(std::memory_order_acquire))
        {
            if (validation_->status.code() == RHIErrorCode::NotReady && validation_attempts_ < 120u)
            {
                auto next = std::make_shared<MaterialShaderMapValidation>();
                next->shader_map = candidate_configurations_[validation_configuration_];
                validation_ = std::move(next);
                validation_sent_ = false;
                ++validation_attempts_;
                return;
            }
            if (!validation_->status.succeeded())
            {
                const auto failure = validation_->status.message();
                validation_.reset();
                candidate_.reset();
                candidate_configurations_.clear();
                completed_material_validations_.clear();
                validated_material_targets_.clear();
                material_targets_captured_ = false;
                const auto* source = find(request_name_);
                if (restoring_ && saved_candidate_ && source && !source->artifacts.empty())
                {
                    saved_candidate_ = false;
                    std::string error;
                    if (load_candidate(source->artifacts, request_name_, error))
                    {
                        candidate_relative_ = "deployment/" + source->artifacts.utf8().substr(
                                                                  source->artifacts.utf8().find_last_of("/\\") + 1u);
                        TOY_LOG_WARN("Saved Shader [{}] failed validation: {}. Validating deployed version.",
                                     request_name_, failure);
                        return;
                    }
                    reject(failure + " Deployed version: " + error);
                    return;
                }
                reject(failure);
                return;
            }
            completed_material_validations_.push_back(validation_);
            validation_.reset();
            ++validation_configuration_;
            if (validation_configuration_ < candidate_configurations_.size())
            {
                validation_ = std::make_shared<MaterialShaderMapValidation>();
                validation_->shader_map = candidate_configurations_[validation_configuration_];
                validation_sent_ = false;
                validation_attempts_ = 0u;
                return;
            }
            task_.phase = ShaderTaskPhase::Publishing;
            status_ = "Shader candidate ready.";
        }
        if (candidate_ && !validation_ && find(request_name_)->usage != BuiltinShaderUsage::Material)
        {
            stage_builtin();
        }
    }

    void ShaderWorkflow::collect_validation(std::vector<MaterialShaderMapValidationRef>& requests)
    {
        if (validation_ && !validation_sent_)
        {
            if (collect_material_targets_)
            {
                if (!material_targets_captured_)
                {
                    std::string error;
                    if (!collect_material_targets_(candidate_configurations_, validated_material_targets_, error))
                    {
                        reject(error);
                        return;
                    }
                    material_targets_captured_ = true;
                }
                validation_->targets = validated_material_targets_;
                validation_->exact_targets = true;
            }
            requests.push_back(validation_);
            validation_sent_ = true;
        }
    }

    bool ShaderWorkflow::validate_scene_users(std::string& error) const
    {
        for (const auto& validation : completed_material_validations_)
        {
            for (const auto& revision : validation->scene_revisions)
            {
                if (revision.generation->load(std::memory_order_acquire) != revision.value)
                {
                    error = "Scene geometry, material users or caster roles changed during validation. Recompile.";
                    return false;
                }
            }
        }
        return true;
    }

    bool ShaderWorkflow::validate_candidate_users(std::string& error)
    {
        if (!validate_scene_users(error))
        {
            return false;
        }
        if (!collect_material_targets_ || !candidate_ ||
            candidate_->programs().front()->data().contract.usage != shader::ShaderUsage::Material)
        {
            return true;
        }
        std::vector<MaterialShaderMapValidationTarget> current;
        if (!material_targets_captured_ || !collect_material_targets_(candidate_configurations_, current, error))
        {
            if (error.empty())
            {
                error = "Material candidate users were not validated.";
            }
            return false;
        }
        const auto canonical = [](const std::vector<MaterialShaderMapValidationTarget>& values)
        {
            std::map<MaterialRenderProxy*, ShaderMapCollectionRef> result;
            for (const auto& value : values)
            {
                result.emplace(value.proxy, value.shader_map);
            }
            return result;
        };
        if (canonical(current) != canonical(validated_material_targets_))
        {
            error = "Material users or their static configurations changed during validation. Recompile.";
            return false;
        }
        return true;
    }

    bool ShaderWorkflow::validate_build_settings(const Sha256Hash& expected, std::string& error) const
    {
        shader::ShaderBuildSettings settings;
        if (!shader::read_shader_build_settings(platform_, paths_.engine_build_settings, paths_.project_build_settings,
                                                settings, error))
        {
            return false;
        }
        if (sha256(shader::serialize_shader_build_settings(settings)) != expected)
        {
            error = "Shader build settings changed during candidate preparation. Recompile.";
            return false;
        }
        return true;
    }

    bool ShaderWorkflow::publish()
    {
        if (!candidate_ || validation_)
        {
            error_ = "Shader candidate validation is incomplete.";
            return false;
        }
        if (!validate_scene_users(error_))
        {
            return false;
        }
        const auto* registered = find(request_name_);
        if (!registered)
        {
            error_ = "Shader source registration changed.";
            return false;
        }
        const auto source_text = files_.read_text_utf8(registered->path, maximum_shader_source_bytes);
        if (!source_text.succeeded())
        {
            error_ = "Cannot read Shader source: " + source_text.status().message;
            return false;
        }
        if (sha256(source_text.value()) != source_hash_)
        {
            error_ = "Source changed during pipeline validation. Save files and recompile.";
            return false;
        }
        for (const auto& dependency : candidate_dependencies_)
        {
            const auto path = VirtualPath::parse(dependency.first);
            if (!path.succeeded())
            {
                error_ = path.status().message;
                return false;
            }
            const auto current = files_.read_text_utf8(path.value(), maximum_shader_source_bytes);
            if (!current.succeeded())
            {
                error_ = "Cannot read Shader dependency " + dependency.first + ": " + current.status().message;
                return false;
            }
            if (sha256(current.value()) != dependency.second)
            {
                error_ = "Shader dependency changed during pipeline validation. Recompile: " + dependency.first;
                return false;
            }
        }
        if (!validate_build_settings(build_settings_hash_, error_) || !validate_material_inputs(error_))
        {
            return false;
        }
        if (!restoring_ && !write_publication(request_name_, candidate_relative_, source_hash_))
        {
            return false;
        }
        std::string next_status = "Applied " + request_name_;
        for (auto& source : sources_)
        {
            if (source.name == request_name_)
            {
                source.shader_map = candidate_;
                source.configurations.clear();
                for (const auto& configuration : candidate_configurations_)
                {
                    source.configurations.emplace(configuration->index().permutation_key, configuration);
                }
                source.properties = std::move(candidate_properties_);
                source.diagnostic.clear();
            }
        }
        status_ = std::move(next_status);
        error_.clear();
        candidate_.reset();
        candidate_configurations_.clear();
        completed_material_validations_.clear();
        validated_material_targets_.clear();
        material_targets_captured_ = false;
        candidate_dependencies_.clear();
        finish_batch_item(true);
        if (!busy())
        {
            complete_task();
        }
        return true;
    }

    void ShaderWorkflow::reject(const std::string& error)
    {
        error_ = error.empty() ? "Shader compilation failed." : error;
        status_ = "Failed; previous Shader remains active.";
        const auto* source = find(request_name_);
        if (!batch_cancelled_)
        {
            for (auto& item : sources_)
            {
                if (item.name == request_name_)
                {
                    item.diagnostic = error_;
                }
            }
            if (source && source->usage == BuiltinShaderUsage::Global)
            {
                global_failed_ = true;
            }
            else
            {
                finish_batch_item(false);
            }
            record_task_error(request_name_, error_);
            TOY_LOG_ERROR("Shader [{}]: {}", request_name_, error_);
        }
        candidate_.reset();
        candidate_configurations_.clear();
        completed_material_validations_.clear();
        validated_material_targets_.clear();
        material_targets_captured_ = false;
        validation_.reset();
        candidate_properties_.clear();
        candidate_dependencies_.clear();
        if (!busy())
        {
            complete_task();
        }
    }

    void ShaderWorkflow::shutdown()
    {
        cancel();
        if (worker_)
        {
            worker_->join();
            worker_.reset();
        }
        result_.reset();
        validation_.reset();
        candidate_.reset();
        candidate_configurations_.clear();
        completed_material_validations_.clear();
        validated_material_targets_.clear();
        material_targets_captured_ = false;
        sources_.clear();
        defaults_.reset();
        restore_queue_.clear();
        compile_queue_.clear();
        global_candidates_.clear();
        builtin_revisions_.clear();
        builtin_update_.reset();
        batch_active_ = false;
    }
} // namespace toy3d
