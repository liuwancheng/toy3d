#include "assets/material/material_shader_workflow.h"

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
#include "logging/logger.h"
#include "asset/material/material_asset.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/material/material_asset_builder.h"
#include "misc/utf8.h"

#include "platform/platform_defines.h"

namespace toy3d
{
    namespace
    {
        std::string comparable(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.size() > 1u && path.back() == '/') path.pop_back();
#if WITH_WIN
            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
                relative.find(':') == std::string::npos && parsed.succeeded() && parsed.value().utf8() == "/Source/" + relative;
        }
    }

    MaterialShaderWorkflow::~MaterialShaderWorkflow() { shutdown(); }

    bool MaterialShaderWorkflow::mount(const PhysicalPath& root, const std::string& name, bool writable, std::string& error)
    {
        DirectoryFileStoreDesc desc; desc.physical_root = root; desc.writable = writable;
        const auto store = DirectoryFileStore::create(platform_, desc);
        if (!store.succeeded()) { error = store.status().message; return false; }
        const auto path = VirtualPath::parse(name);
        if (!path.succeeded()) { error = path.status().message; return false; }
        FileMountDesc mounted; mounted.virtual_root = path.value(); mounted.store = store.value();
        mounted.access = writable ? MountAccess::ReadWrite : MountAccess::ReadOnly;
        const auto added = files_.add_mount(mounted);
        if (!added.succeeded()) { error = added.message; return false; }
        return true;
    }

    bool MaterialShaderWorkflow::initialize(MaterialShaderPaths paths, MaterialRef defaults, std::string& error)
    {
        paths_ = std::move(paths); defaults_ = std::move(defaults);
        if (!defaults_ || !defaults_->desc().shader_program) { error = "Default material Shader is unavailable."; return false; }
        const auto made = platform_.create_directories(paths_.saved);
        if (!made.succeeded()) { error = made.message; return false; }
        if (!mount(paths_.engine_shader, "/Engine/Shaders", false, error) ||
            !mount(paths_.engine_include, "/Engine/ShaderIncludes", false, error) ||
            !mount(paths_.project_shader, "/Project/Shaders", false, error) ||
            !mount(paths_.project_config, "/Project/Config", false, error) ||
            !mount(paths_.saved, "/Saved", true, error)) return false;
        const auto include = platform_.join_relative(paths_.project_shader, "include");
        if (!include.succeeded()) { error = include.status().message; return false; }
        const auto exists = platform_.exists(include.value());
        if (!exists.succeeded()) { error = exists.status().message; return false; }
        if (exists.value() && !mount(include.value(), "/Project/ShaderIncludes", false, error)) return false;
        const auto frozen = files_.freeze();
        if (!frozen.succeeded()) { error = frozen.message; return false; }
        if (!read_sources(error)) return false;
        for (const auto& source : sources_)
        {
            const auto pointer = VirtualPath::parse("/Saved/" + sha256_to_hex(sha256(source.name)) + "/current.txt");
            if (!pointer.succeeded()) { error = pointer.status().message; return false; }
            const auto existing = files_.stat(pointer.value());
            if (existing.succeeded()) restore_queue_.push_back(source.name);
            else if (existing.status().code != FileErrorCode::NotFound)
            { error = existing.status().message; return false; }
        }
        status_ = "Ready. Save source in VS Code, then Recompile.";
        return true;
    }

    bool MaterialShaderWorkflow::read_sources(std::string& error)
    {
        std::vector<MaterialShaderSource> values;
        const auto builtin = VirtualPath::parse("/Engine/Shaders/surface/phong.shader");
        if (!builtin.succeeded()) { error = builtin.status().message; return false; }
        values.push_back({defaults_->desc().shader_name, builtin.value(), defaults_->desc().shader_program, {}});
        if (!paths_.builtin_entries.empty())
        {
            const auto entries = platform_.enumerate_directory(paths_.builtin_entries);
            if (!entries.succeeded()) { error = entries.status().message; return false; }
            for (const auto& entry : entries.value())
            {
                if (entry.type != FileType::Directory) continue;
                const auto basename = entry.path.utf8().substr(entry.path.utf8().find_last_of("/\\") + 1u);
                // optional distinguishes non-entry directories from valid entry hashes.
                if (!sha256_from_hex(basename)) continue;
                if (!shader::read_shader_editor_properties(platform_, entry.path, values.front().name,
                    values.front().program->data().parameter_schema, values.front().properties, error)) return false;
            }
        }
        const auto manifest = VirtualPath::parse("/Project/Config/shader_sources.txt");
        if (!manifest.succeeded()) { error = manifest.status().message; return false; }
        const auto text = files_.read_text_utf8(manifest.value(), 64u * 1024u);
        if (!text.succeeded()) { error = "Cannot read project Shader source manifest: " + text.status().message; return false; }
        std::istringstream input(text.value()); std::string line;
        if (!std::getline(input, line)) { error = "Empty Shader source manifest."; return false; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "Toy3dShaderSources 1") { error = "Unsupported Shader source manifest header."; return false; }
        std::set<std::string> names, paths;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('\t');
            if (separator == std::string::npos || line.find('\t', separator + 1u) != std::string::npos)
            { error = "Shader source entries require logical name, TAB, relative path."; return false; }
            MaterialShaderSource source; source.name = line.substr(0, separator);
            const std::string relative = line.substr(separator + 1u);
            MaterialAssetData descriptor; descriptor.shader_name = source.name;
            if (values.size() >= 256u || source.name.compare(0, 16u, "Project/Surface/") != 0 ||
                !validate_material_asset(descriptor).succeeded() || !safe_relative(relative) ||
                relative.size() < 7u || relative.compare(relative.size() - 7u, 7u, ".shader") != 0 ||
                !names.insert(source.name).second)
            { error = "Invalid, duplicate or oversized project Shader source entry."; return false; }
            const auto parsed = VirtualPath::parse("/Project/Shaders/" + relative);
            if (!parsed.succeeded()) { error = parsed.status().message; return false; }
            source.path = parsed.value();
            PhysicalPath physical;
            if (!physical_source(source, physical, error)) return false;
            if (!paths.insert(comparable(physical.utf8())).second)
            { error = "Multiple Shader names refer to the same physical source."; return false; }
            values.push_back(std::move(source));
        }
        sources_ = std::move(values);
        return true;
    }

    const MaterialShaderSource* MaterialShaderWorkflow::find(const std::string& name) const
    { for (const auto& source : sources_) if (source.name == name) return &source; return nullptr; }
    ShaderMapProgramRef MaterialShaderWorkflow::program(const std::string& name) const
    { const auto* source = find(name); return source ? source->program : nullptr; }

    bool MaterialShaderWorkflow::physical_source(const MaterialShaderSource& source, PhysicalPath& output, std::string& error) const
    {
        const bool engine = source.path.utf8().compare(0, 16u, "/Engine/Shaders/") == 0;
        const PhysicalPath& root = engine ? paths_.engine_shader : paths_.project_shader;
        const std::string prefix = engine ? "/Engine/Shaders/" : "/Project/Shaders/";
        if (source.path.utf8().compare(0, prefix.size(), prefix) != 0) { error = "Shader source has no allowed root."; return false; }
        const auto joined = platform_.join_relative(root, source.path.utf8().substr(prefix.size()));
        if (!joined.succeeded()) { error = joined.status().message; return false; }
        const auto canonical_root = platform_.canonical(root);
        const auto canonical_file = platform_.canonical(joined.value());
        if (!canonical_root.succeeded() || !canonical_file.succeeded() || !contains(canonical_root.value(), canonical_file.value()))
        { error = "Shader source is missing or escapes its registered root."; return false; }
        const auto checked = files_.read_text_utf8(source.path, 4u * 1024u * 1024u);
        if (!checked.succeeded()) { error = checked.status().message; return false; }
        output = canonical_file.value(); return true;
    }

    bool MaterialShaderWorkflow::open_source(const std::string& name, std::uint32_t line, std::uint32_t column)
    {
        const auto* source = find(name); PhysicalPath physical;
        if (!source || !physical_source(*source, physical, error_)) { if (!source) error_ = "Shader source is not registered: " + name; return false; }
        PhysicalPath executable = paths_.code_executable;
        if (executable.empty())
        {
            std::vector<std::string> candidates;
#if WITH_WIN
            if (const char* local = std::getenv("LOCALAPPDATA")) candidates.push_back(std::string(local) + "/Programs/Microsoft VS Code/Code.exe");
            if (const char* profile = std::getenv("USERPROFILE")) candidates.push_back(std::string(profile) + "/Program Files/Microsoft VS Code/Code.exe");
            if (const char* installed = std::getenv("ProgramFiles")) candidates.push_back(std::string(installed) + "/Microsoft VS Code/Code.exe");
#elif WITH_MAC
            candidates.push_back("/Applications/Visual Studio Code.app/Contents/MacOS/Electron");
#else
            candidates.push_back("/usr/share/code/code"); candidates.push_back("/usr/bin/code");
#endif
            for (const auto& candidate : candidates)
            {
                const auto exists = platform_.exists(PhysicalPath(candidate));
                if (!exists.succeeded()) { error_ = exists.status().message; return false; }
                if (exists.value()) { executable = PhysicalPath(candidate); break; }
            }
        }
        if (executable.empty()) { error_ = "VS Code was not found. Start Editor with --Editor.CodeExecutable=<absolute Code.exe path>."; return false; }
        const auto opened = processes_.launch_detached(executable,
            {"--reuse-window", "--goto", physical.utf8() + ":" + std::to_string(std::max(line, 1u)) + ":" + std::to_string(std::max(column, 1u))});
        if (!opened.succeeded()) { error_ = opened.message; TOY_LOG_ERROR("Open material source: {}", error_); return false; }
        error_.clear(); status_ = "Opened source in VS Code: " + physical.utf8(); return true;
    }

    bool MaterialShaderWorkflow::recompile(const std::string& name, AssetId origin, std::uint64_t session_revision)
    {
        if (busy()) { error_ = "A Shader compile/publication is already in progress."; return false; }
        const auto* source = find(name); PhysicalPath physical;
        if (!source || !physical_source(*source, physical, error_)) { if (!source) error_ = "Shader is not registered."; return false; }
        const auto text = files_.read_text_utf8(source->path, 4u * 1024u * 1024u);
        if (!text.succeeded()) { error_ = text.status().message; return false; }
        if (!AssetId::try_generate(request_id_)) { error_ = "Unable to allocate Shader compile request identity."; return false; }
        request_name_ = name; origin_ = origin; origin_revision_ = session_revision; source_hash_ = sha256(text.value());
        candidate_relative_ = "requests/" + request_id_.hex();
        const auto directory = platform_.join_relative(paths_.saved, candidate_relative_);
        if (!directory.succeeded()) { error_ = directory.status().message; return false; }
        request_directory_ = directory.value();
        const auto made = platform_.create_directories(request_directory_);
        if (!made.succeeded()) { error_ = made.message; return false; }
        const auto output = platform_.join_relative(request_directory_, "entries");
        const auto work = platform_.join_relative(request_directory_, "work");
        if (!output.succeeded() || !work.succeeded()) { error_ = "Unable to resolve Shader compile output paths."; return false; }
        std::vector<std::string> arguments = {"--toolchain-root", paths_.toolchain.utf8(), "compile-vulkan", physical.utf8(),
            source->path.utf8(), "Forward", output.value().utf8(), work.value().utf8(), "--engine-include-root", paths_.engine_include.utf8()};
        const auto project_include = platform_.join_relative(paths_.project_shader, "include");
        if (!project_include.succeeded()) { error_ = project_include.status().message; return false; }
        const auto includes_exist = platform_.exists(project_include.value());
        if (!includes_exist.succeeded()) { error_ = includes_exist.status().message; return false; }
        if (includes_exist.value()) { arguments.push_back("--project-include-root"); arguments.push_back(project_include.value().utf8()); }
        cancel_.store(false); result_ = std::make_shared<CompileResult>();
        try
        {
            worker_ = std::make_unique<Thread>(threads_, "MaterialShaderCompiler", [this, result = result_, arguments = std::move(arguments)]()
            {
                try
                {
                    ProcessRunOptions options; options.timeout_ms = 120000u; options.cancel = &cancel_;
                    result->process = processes_.run(paths_.compiler, arguments, options);
                }
                catch (const std::exception& error) { result->process.error = ProcessError::Launch; result->process.message = error.what(); }
                result->complete.store(true, std::memory_order_release);
            });
        }
        catch (const std::exception& error) { result_.reset(); error_ = error.what(); return false; }
        error_.clear(); output_.clear(); status_ = "Compiling " + name + " (saved files)..."; return true;
    }

    bool MaterialShaderWorkflow::error_location(std::uint32_t& line, std::uint32_t& column) const
    {
        const auto* source = find(request_name_);
        if (!source || error_.empty()) return false;
        const std::string prefix = source->path.utf8() + ":";
        // Prefer DXC's concrete error over the compiler's outer failure message.
        std::istringstream input(output_); std::string message;
        bool found = false;
        while (std::getline(input, message))
        {
            const auto offset = message.find(prefix);
            if (offset == std::string::npos) continue;
            const char* begin = message.data() + offset + prefix.size();
            const char* end = message.data() + message.size();
            std::uint32_t source_line = 0u, source_column = 0u;
            // from_chars reads bounded compiler text without locale or exceptions.
            const auto parsed_line = std::from_chars(begin, end, source_line);
            if (parsed_line.ec != std::errc{} || parsed_line.ptr == end || *parsed_line.ptr != ':') continue;
            const auto parsed_column = std::from_chars(parsed_line.ptr + 1, end, source_column);
            if (parsed_column.ec != std::errc{} || !source_line || !source_column) continue;
            line = source_line; column = source_column; found = true;
            if (message.find(": error:", offset) != std::string::npos) return true;
        }
        return found;
    }

    bool MaterialShaderWorkflow::has_error_location() const
    { std::uint32_t line = 0u, column = 0u; return error_location(line, column); }
    bool MaterialShaderWorkflow::open_error()
    {
        std::uint32_t line = 0u, column = 0u;
        if (!error_location(line, column)) { error_ = "No registered source location was found in compiler output."; return false; }
        return open_source(request_name_, line, column);
    }

    bool MaterialShaderWorkflow::validate_interface(const ShaderMapProgram& candidate, std::string& error) const
    {
        if (candidate.data().pass_name != "Forward" || candidate.data().platform != ShaderPlatform::VulkanES31)
        { error = "Only the current Vulkan ES3.1 Forward compile target is available."; return false; }
        for (const auto& binding : candidate.data().bindings)
        {
            if (binding.group == RHIBindingGroup::Material) continue;
            const auto& known = defaults_->desc().shader_program->data().bindings;
            const auto found = std::find_if(known.begin(), known.end(), [&binding](const ShaderMapBinding& value)
            { return value.group == binding.group && value.parameter_id == binding.parameter_id; });
            if (found == known.end() || found->type != binding.type || found->array_count != binding.array_count ||
                found->data_layout_hash != binding.data_layout_hash || found->constant_buffer_size != binding.constant_buffer_size ||
                found->shader_abi_version != binding.shader_abi_version)
            { error = "Shader changed an engine-owned View, Object, Global or Forward lighting contract."; return false; }
        }
        return true;
    }

    bool MaterialShaderWorkflow::load_candidate(const PhysicalPath& directory, const std::string& name, std::string& error)
    {
        ShaderMapEntryLoader loader(directory);
        ShaderMapProgramKey key{name, "Forward", ShaderPlatform::VulkanES31};
        auto data = loader.load_program(key);
        if (!data.succeeded()) { error = data.error; return false; }
        auto candidate = ShaderMap::create_candidate(std::move(*data.program), key);
        if (!candidate.succeeded()) { error = candidate.error; return false; }
        if (!validate_interface(*candidate.program, error)) return false;
        // Validate the supported material model even when no window or scene
        // user exists yet. Reuse the runtime builder, including its resource
        // and default-value rules, before calling a Shader ready for creation.
        MaterialTextureValues textures;
        for (const auto& resource : defaults_->parameter_schema().resources)
        {
            const auto found = defaults_->desc().texture_defaults.find(resource.parameter_id);
            if (found != defaults_->desc().texture_defaults.end()) textures.named_defaults[resource.default_value] = found->second;
        }
        MaterialInstanceRef checked;
        {
            MaterialAssetData descriptor; descriptor.shader_name = name;
            const auto built = create_material_from_asset(descriptor, candidate.program, textures);
            if (!built.succeeded()) { error = built.status().message; return false; }
            checked = built.value();
        }
        MaterialInstance::release(checked);
        const auto directories = platform_.enumerate_directory(directory);
        if (!directories.succeeded()) { error = directories.status().message; return false; }
        std::vector<shader::ShaderEditorProperty> properties;
        std::map<std::string, Sha256Hash> dependencies;
        for (const auto& entry : directories.value())
        {
            if (entry.type != FileType::Directory) continue;
            const auto basename = entry.path.utf8().substr(entry.path.utf8().find_last_of("/\\") + 1u);
            const auto hash = sha256_from_hex(basename);
            if (!hash) continue;
            const auto verified = shader::read_verified_shader_map_entry(platform_, directory, *hash);
            if (!verified.succeeded()) { error = "ShaderMapEntry changed during publication."; return false; }
            for (const auto& stage : verified.entry->stages)
                for (const auto& dependency : stage.request.dependencies)
                {
                    if (dependency.virtual_path.compare(0, 11u, "/Generated/") == 0 || dependency.virtual_path.compare(0, 10u, "builtin://") == 0) continue;
                    const auto path = VirtualPath::parse(dependency.virtual_path);
                    if (!path.succeeded()) { error = path.status().message; return false; }
                    const auto current = files_.read_text_utf8(path.value(), 4u * 1024u * 1024u);
                    if (!current.succeeded() || sha256(current.value()) != dependency.content_hash)
                    { error = "Shader dependency changed or disappeared. Save files and recompile: " + dependency.virtual_path; return false; }
                    dependencies[dependency.virtual_path] = dependency.content_hash;
                }
            if (!shader::read_shader_editor_properties(platform_, entry.path, name, candidate.program->data().parameter_schema, properties, error)) return false;
        }
        candidate_ = std::move(candidate.program); candidate_properties_ = std::move(properties);
        candidate_dependencies_ = std::move(dependencies);
        validation_ = std::make_shared<MaterialProgramValidation>(); validation_->program = candidate_;
        validation_sent_ = false; validation_attempts_ = 0u;
        return true;
    }

    void MaterialShaderWorkflow::tick()
    {
        if (!busy() && !restore_queue_.empty())
        {
            request_name_ = restore_queue_.back(); restore_queue_.pop_back(); origin_ = {}; origin_revision_ = 0u;
            const auto pointer = VirtualPath::parse("/Saved/" + sha256_to_hex(sha256(request_name_)) + "/current.txt");
            const auto text = files_.read_text_utf8(pointer.value(), 512u);
            if (!text.succeeded()) { reject(text.status().message); return; }
            std::istringstream input(text.value()); std::string signature;
            std::getline(input, candidate_relative_); std::getline(input, signature);
            const std::string prefix = "requests/";
            const auto hash = sha256_from_hex(signature);
            AssetId saved_request;
            if (!hash || !safe_relative(candidate_relative_) || candidate_relative_.compare(0, prefix.size(), prefix) != 0 ||
                !AssetId::parse(candidate_relative_.substr(prefix.size()), saved_request))
            { reject("Invalid saved Shader publication record. Recompile the source."); return; }
            source_hash_ = *hash;
            const auto* source = find(request_name_);
            const auto current = files_.read_text_utf8(source->path, 4u * 1024u * 1024u);
            if (!current.succeeded() || sha256(current.value()) != source_hash_)
            { reject("Saved Shader source changed. Recompile " + request_name_); return; }
            const auto directory = platform_.join_relative(paths_.saved, candidate_relative_ + "/entries");
            std::string error;
            if (!directory.succeeded() || !load_candidate(directory.value(), request_name_, error)) { reject(error); return; }
            status_ = "Validating saved Shader...";
        }
        if (worker_ && result_->complete.load(std::memory_order_acquire))
        {
            worker_->join(); worker_.reset();
            output_ = result_->process.output;
            if (!result_->process.succeeded()) { reject(result_->process.message + "\n" + output_); result_.reset(); return; }
            result_.reset();
            const auto* source = find(request_name_);
            if (!source) { reject("Shader source registration changed during compilation."); return; }
            const auto text = files_.read_text_utf8(source->path, 4u * 1024u * 1024u);
            if (!text.succeeded() || sha256(text.value()) != source_hash_)
            { reject("Source changed during compilation. Save it and recompile."); return; }
            const auto entries = platform_.join_relative(request_directory_, "entries");
            std::string error;
            if (!entries.succeeded() || !load_candidate(entries.value(), request_name_, error)) { reject(error); return; }
            status_ = "Validating Shader pipeline on Rendering Thread...";
        }
        if (validation_ && validation_->complete.load(std::memory_order_acquire))
        {
            if (validation_->status.code() == RHIErrorCode::NotReady && validation_attempts_ < 120u)
            {
                auto next = std::make_shared<MaterialProgramValidation>(); next->program = candidate_;
                validation_ = std::move(next); validation_sent_ = false; ++validation_attempts_; return;
            }
            if (!validation_->status.succeeded()) { reject(validation_->status.message()); return; }
            validation_.reset(); status_ = "Shader candidate ready.";
        }
    }

    void MaterialShaderWorkflow::collect_validation(std::vector<MaterialProgramValidationRef>& requests)
    { if (validation_ && !validation_sent_) { requests.push_back(validation_); validation_sent_ = true; } }

    bool MaterialShaderWorkflow::publish()
    {
        if (!candidate_ || validation_) { error_ = "Shader candidate validation is incomplete."; return false; }
        const auto* registered = find(request_name_);
        if (!registered) { error_ = "Shader source registration changed."; return false; }
        const auto source_text = files_.read_text_utf8(registered->path, 4u * 1024u * 1024u);
        if (!source_text.succeeded() || sha256(source_text.value()) != source_hash_)
        { error_ = "Source changed during pipeline validation. Save files and recompile."; return false; }
        for (const auto& dependency : candidate_dependencies_)
        {
            const auto path = VirtualPath::parse(dependency.first);
            if (!path.succeeded()) { error_ = path.status().message; return false; }
            const auto current = files_.read_text_utf8(path.value(), 4u * 1024u * 1024u);
            if (!current.succeeded() || sha256(current.value()) != dependency.second)
            { error_ = "Shader dependency changed during pipeline validation. Recompile: " + dependency.first; return false; }
        }
        const auto pointer = VirtualPath::parse("/Saved/" + sha256_to_hex(sha256(request_name_)) + "/current.txt");
        if (!pointer.succeeded()) { error_ = pointer.status().message; return false; }
        const auto record_directory = platform_.join_relative(paths_.saved, sha256_to_hex(sha256(request_name_)));
        if (!record_directory.succeeded()) { error_ = "Cannot resolve Shader publication directory: " + record_directory.status().message; return false; }
        const auto made = platform_.create_directories(record_directory.value());
        if (!made.succeeded()) { error_ = "Cannot create Shader publication directory: " + made.message; return false; }
        const std::string text = candidate_relative_ + "\n" + sha256_to_hex(source_hash_) + "\n";
        std::string next_status = "Compiled and applied " + request_name_;
        const auto saved = files_.write_binary_atomic(pointer.value(), std::vector<std::uint8_t>(text.begin(), text.end()), FilePublishMode::Replace);
        if (!saved.succeeded()) { error_ = "Cannot save Shader publication record: " + saved.message; return false; }
        for (auto& source : sources_) if (source.name == request_name_)
        { source.program = candidate_; source.properties = std::move(candidate_properties_); }
        status_ = std::move(next_status); error_.clear();
        candidate_.reset(); candidate_dependencies_.clear();
        return true;
    }

    void MaterialShaderWorkflow::reject(const std::string& error)
    { error_ = error.empty() ? "Shader compilation failed." : error; status_ = "Failed; previous material remains active."; candidate_.reset(); validation_.reset(); candidate_properties_.clear(); candidate_dependencies_.clear(); TOY_LOG_ERROR("Material Shader: {}", error_); }

    void MaterialShaderWorkflow::shutdown()
    {
        cancel_.store(true, std::memory_order_release);
        if (worker_) { worker_->join(); worker_.reset(); }
        result_.reset(); validation_.reset(); candidate_.reset(); sources_.clear(); defaults_.reset();
    }
}
