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
#include "logging/logger.h"
#include "asset/material/material_asset.h"
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
    ShaderMapCollectionRef ShaderWorkflow::shader_map(const std::string& name) const
    {
        const auto* source = find(name);
        return source ? source->shader_map : nullptr;
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
        std::vector<std::string> arguments = {"--toolchain-root",      paths_.toolchain.utf8(),
                                              "compile-vulkan",        physical.utf8(),
                                              source->path.utf8(),     source->pass,
                                              output.value().utf8(),   work.value().utf8(),
                                              "--engine-include-root", paths_.engine_include.utf8()};
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
            const auto reference =
                defaults_->desc().shader_map->find(shader::ShaderPassRole::Forward, data.contract.vertex_factory);
            if (!reference.succeeded())
            {
                error = reference.error;
                return false;
            }
            if (data.contract.role != shader::ShaderPassRole::Forward)
            {
                const ShadowDepthPassParameters shadow;
                const HitProxyPassParameters hit;
                const auto& metadata = data.contract.role == shader::ShaderPassRole::ShadowDepth
                                           ? shader_parameters_metadata(shadow)
                                           : shader_parameters_metadata(hit);
                const auto status = validate_shader_parameters_group_against_schema(metadata, data.parameter_schema);
                if (!status)
                {
                    error = "Material mesh Pass parameters must match the engine role: " + status.message();
                    return false;
                }
            }
            for (const auto& binding : data.bindings)
            {
                if (binding.group == RHIBindingGroup::Material ||
                    (binding.group == RHIBindingGroup::Pass && data.contract.role != shader::ShaderPassRole::Forward))
                {
                    continue;
                }
                const auto& known = reference.program->data().bindings;
                const auto found =
                    std::find_if(known.begin(), known.end(),
                                 [&binding](const ShaderMapBinding& value)
                                 {
                                     return value.group == binding.group && value.parameter_id == binding.parameter_id;
                                 });
                if (found == known.end() || found->type != binding.type || found->array_count != binding.array_count ||
                    found->data_layout_hash != binding.data_layout_hash ||
                    found->constant_buffer_size != binding.constant_buffer_size ||
                    found->shader_abi_version != binding.shader_abi_version)
                {
                    error = "Shader changed an engine-owned View, Object, Global or Forward lighting contract.";
                    return false;
                }
            }
        }
        return true;
    }

    bool ShaderWorkflow::load_candidate(const PhysicalPath& directory, const std::string& name, std::string& error)
    {
        // Cache locators stay within the configured Saved/deployment roots,
        // including when an on-disk directory is a link or junction.
        const auto canonical = platform_.canonical(directory);
        const auto saved = platform_.canonical(paths_.saved);
        const auto builtin = platform_.canonical(paths_.builtin_root);
        if (!canonical.succeeded())
        {
            error = "Cannot resolve Shader artifacts " + directory.utf8() + ": " + canonical.status().message;
            return false;
        }
        if (!((saved.succeeded() && contains(saved.value(), canonical.value())) ||
              (builtin.succeeded() && contains(builtin.value(), canonical.value()))))
        {
            error = "Shader artifact directory escapes its configured root: " + directory.utf8();
            return false;
        }
        ShaderMapEntryLoader loader(canonical.value());
        const auto* source = find(name);
        if (!source)
        {
            error = "Shader source is not registered.";
            return false;
        }
        ShaderMapProgramKey key{name, source->pass, ShaderPlatform::VulkanES31};
        const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!text.succeeded())
        {
            error = text.status().message;
            return false;
        }
        // Reuse the compiler's typed default selection, including nonempty
        // domains (Unlit has USE_VERTEX_COLOR=false). An empty-domain key does
        // not mean "default" for every Shader; never choose the first cache entry.
        const auto parsed = shader::parse_shader(text.value(), source->path.utf8());
        if (!parsed.succeeded())
        {
            error = "Cannot parse registered Shader source.";
            for (const auto& diagnostic : parsed.diagnostics)
            {
                error += "\n" + shader::format_diagnostic(diagnostic);
            }
            return false;
        }
        if (parsed.asset->name != name)
        {
            error = "Shader declaration does not match its registered name.";
            return false;
        }
        const auto permutation = shader::resolve_shader_permutation(*parsed.asset, {});
        if (!permutation.succeeded())
        {
            error = "Cannot resolve default Shader permutation.";
            return false;
        }
        key.permutation_key = permutation.permutation->key;
        const auto pass = std::find_if(parsed.asset->passes.begin(), parsed.asset->passes.end(),
                                       [&](const shader::ShaderPass& value)
                                       {
                                           return value.name == source->pass;
                                       });
        if (pass == parsed.asset->passes.end())
        {
            error = "Registered pass is missing from Shader declaration.";
            return false;
        }
        key.role = pass->role;
        key.vertex_factory = parsed.asset->usage == shader::ShaderUsage::Global
                                 ? shader::VertexFactoryType::None
                                 : (shader::supports_vertex_factory(parsed.asset->vertex_factory_support,
                                                                    shader::VertexFactoryType::Local)
                                        ? shader::VertexFactoryType::Local
                                        : shader::VertexFactoryType::GPUSkin);
        auto candidate =
            ShaderMapCollection::create_candidate(loader.load_collection(name, key.platform, key.permutation_key));
        if (!candidate.succeeded())
        {
            error = candidate.error;
            return false;
        }
        if (source->usage == BuiltinShaderUsage::Material && !validate_interface(*candidate.collection, error))
        {
            return false;
        }
        if (source->usage == BuiltinShaderUsage::Material)
        {
            // Validate the supported material model even when no window or scene
            // user exists yet. Reuse the runtime builder, including its resource
            // and default-value rules, before calling a Shader ready for creation.
            MaterialTextureValues textures;
            for (const auto& resource : defaults_->parameter_schema().resources)
            {
                const auto found = defaults_->desc().texture_defaults.find(resource.parameter_id);
                if (found != defaults_->desc().texture_defaults.end())
                {
                    textures.named_defaults[resource.default_value] = found->second;
                }
            }
            MaterialInstanceRef checked;
            {
                MaterialAssetData descriptor;
                descriptor.shader_name = name;
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
        shader::ShaderMapIndex index;
        if (!shader::read_shader_map_index(platform_, directory, name, shader::ShaderTarget::VulkanSpirV,
                                           shader::ShaderCompileProfile::VulkanES31, key.permutation_key, index, error))
        {
            return false;
        }
        if (index.source_hash != sha256(text.value()))
        {
            error = "Shader source changed. Save files and recompile: " + source->path.utf8();
            return false;
        }
        std::vector<shader::ShaderEditorProperty> properties;
        std::map<std::string, Sha256Hash> dependencies;
        for (const auto& record : index.programs)
        {
            const auto verified = shader::read_verified_shader_map_entry(platform_, directory, record.entry_key);
            if (!shader::shader_map_index_matches_entry(index, record, verified))
            {
                error = "Cannot verify indexed ShaderMapEntry " + sha256_to_hex(record.entry_key);
                for (const auto& diagnostic : verified.diagnostics)
                {
                    error += "\n" + diagnostic;
                }
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
                    if (!current.succeeded())
                    {
                        error = "Cannot read Shader dependency " + dependency.virtual_path + ": " +
                                current.status().message;
                        return false;
                    }
                    if (sha256(current.value()) != dependency.content_hash)
                    {
                        error = "Shader dependency changed. Save files and recompile: " + dependency.virtual_path;
                        return false;
                    }
                    dependencies[dependency.virtual_path] = dependency.content_hash;
                }
            }
            if (source->usage == BuiltinShaderUsage::Material &&
                verified.entry->contract.vertex_factory == key.vertex_factory &&
                verified.entry->contract.role == key.role && verified.entry->pass_name == key.pass_name &&
                !shader::read_shader_editor_properties(
                    platform_, *verified.entry_directory, name,
                    candidate.collection->find(key.role, key.vertex_factory).program->data().parameter_schema,
                    properties, error))
            {
                return false;
            }
        }
        candidate_ = std::move(candidate.collection);
        candidate_properties_ = std::move(properties);
        candidate_dependencies_ = std::move(dependencies);
        if (source->usage == BuiltinShaderUsage::Material)
        {
            validation_ = std::make_shared<MaterialShaderMapValidation>();
            validation_->shader_map = candidate_;
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
                next->shader_map = candidate_;
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
            validation_.reset();
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
            requests.push_back(validation_);
            validation_sent_ = true;
        }
    }

    bool ShaderWorkflow::publish()
    {
        if (!candidate_ || validation_)
        {
            error_ = "Shader candidate validation is incomplete.";
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
                source.properties = std::move(candidate_properties_);
                source.diagnostic.clear();
            }
        }
        status_ = std::move(next_status);
        error_.clear();
        candidate_.reset();
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
