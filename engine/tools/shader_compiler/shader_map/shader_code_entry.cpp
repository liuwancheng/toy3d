#include "shader_map/shader_code_entry.h"

#include <fstream>
#include <sstream>

namespace toy3d::shader
{
    namespace
    {
        bool hash_is_zero(const Sha256Hash& hash)
        {
            for (std::uint8_t byte : hash)
                if (byte != 0u) return false;
            return true;
        }

        bool write_text(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            return static_cast<bool>(output);
        }

        bool write_binary(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            if (!bytes.empty())
                output.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            output.close();
            return static_cast<bool>(output);
        }

        void add_error(
            ShaderCodeEntryWriteResult& result,
            const ShaderCompileRequest& request,
            const std::string& message)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::ShaderCodeWriteFailed,
                {request.source_virtual_path, 0, 1, 1}, message});
        }
    }

    bool ShaderCodeEntryWriteResult::succeeded() const
    {
        return entry_directory.has_value() && diagnostics.empty();
    }

    std::string serialize_shader_stage_reflection(const ShaderStageReflection& reflection)
    {
        std::ostringstream output;
        output << "reflection_version=1\n"
               << "stage=" << static_cast<std::uint32_t>(reflection.stage) << '\n'
               << "entry_point=" << reflection.entry_point << '\n'
               << "reflection_hash=" << sha256_to_hex(reflection.reflection_hash) << '\n'
               << "thread_group_size=" << reflection.thread_group_size_x << ','
               << reflection.thread_group_size_y << ',' << reflection.thread_group_size_z << '\n';
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            output << "binding=" << binding.parameter_id << '\t' << binding.name << '\t'
                   << static_cast<std::uint32_t>(binding.group) << '\t'
                   << static_cast<std::uint32_t>(binding.category) << '\t'
                   << (binding.resource_kind ? static_cast<std::uint32_t>(*binding.resource_kind) : 0xffffffffu) << '\t'
                   << static_cast<std::uint32_t>(binding.stages) << '\t'
                   << binding.array_count << '\t' << binding.descriptor_set << '\t'
                   << binding.descriptor_binding << '\t' << binding.constant_buffer_size << '\n';
            for (const ReflectedConstantMember& member : binding.constant_members)
            {
                output << "member=" << binding.name << '\t' << member.parameter_id << '\t'
                       << member.name << '\t'
                       << static_cast<std::uint32_t>(member.type) << '\t' << member.offset << '\t'
                       << member.size << '\t' << member.array_stride << '\t'
                       << member.matrix_stride << '\n';
            }
        }
        for (const ReflectedInterfaceVariable& variable : reflection.interface_variables)
        {
            output << "interface=" << (variable.input ? "input" : "output") << '\t'
                   << variable.location << '\t' << variable.name << '\t' << variable.semantic << '\t'
                   << static_cast<std::uint32_t>(variable.scalar_type) << '\t'
                   << variable.component_count << '\n';
        }
        return output.str();
    }

    ShaderCodeEntryWriteResult write_verified_shader_code_entry(
        const std::filesystem::path& entry_root,
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const ShaderStageReflection& reflection,
        const std::vector<std::uint8_t>& binary)
    {
        ShaderCodeEntryWriteResult result;
        if (entry_root.empty() || binary.empty() || request.target != ShaderTarget::VulkanSpirV ||
            request.profile != ShaderCompileProfile::VulkanPortableV1 ||
            target_layout.target != request.target ||
            target_layout.mapping_version != vulkan_binding_mapping_version ||
            target_layout.target_binding_hash != request.target_binding_hash ||
            reflection.stage != request.stage || reflection.entry_point != request.entry_point ||
            hash_is_zero(request.compile_key) || hash_is_zero(request.logical_layout_hash) ||
            hash_is_zero(target_layout.target_binding_hash) ||
            hash_is_zero(reflection.reflection_hash))
        {
            add_error(result, request,
                "ShaderCodeEntry publication requires validated binary, request, mapping, and reflection records.");
            return result;
        }

        const std::string key = sha256_to_hex(request.compile_key);
        const std::filesystem::path final_directory = entry_root / key;
        const std::filesystem::path staging_directory = entry_root / (key + ".tmp");
        std::error_code error;
        std::filesystem::create_directories(entry_root, error);
        if (error || std::filesystem::exists(final_directory) || std::filesystem::exists(staging_directory))
        {
            add_error(result, request,
                error ? "Failed to create ShaderCodeEntry root: " + error.message() :
                    "ShaderCodeEntry compile-key directory already exists.");
            return result;
        }
        if (!std::filesystem::create_directory(staging_directory, error) || error)
        {
            add_error(result, request, "Failed to create ShaderCodeEntry staging directory: " + error.message());
            return result;
        }

        const Sha256Hash binary_hash = sha256(binary);
        std::ostringstream manifest;
        manifest << "shader_code_entry_version=" << shader_code_entry_version << '\n'
                 << "compile_key=" << key << '\n'
                 << "target=" << static_cast<std::uint32_t>(request.target) << '\n'
                 << "profile=" << static_cast<std::uint32_t>(request.profile) << '\n'
                 << "stage=" << static_cast<std::uint32_t>(request.stage) << '\n'
                 << "entry_point=" << request.entry_point << '\n'
                 << "source_virtual_path=" << request.source_virtual_path << '\n'
                 << "compiler_identity=" << request.compiler_identity << '\n'
                 << "mapping_version=" << target_layout.mapping_version << '\n'
                 << "logical_layout_hash=" << sha256_to_hex(request.logical_layout_hash) << '\n'
                 << "target_binding_hash=" << sha256_to_hex(target_layout.target_binding_hash) << '\n'
                 << "reflection_hash=" << sha256_to_hex(reflection.reflection_hash) << '\n'
                 << "binary_hash=" << sha256_to_hex(binary_hash) << '\n'
                 << "binary_file=shader.spv\nreflection_file=reflection.txt\ndependencies_file=dependencies.txt\n";
        std::ostringstream dependencies;
        for (const ShaderDependency& dependency : request.dependencies)
            dependencies << dependency.virtual_path << '\t' << sha256_to_hex(dependency.content_hash) << '\n';

        const bool wrote_all =
            write_text(staging_directory / "manifest.txt", manifest.str()) &&
            write_binary(staging_directory / "shader.spv", binary) &&
            write_text(staging_directory / "reflection.txt", serialize_shader_stage_reflection(reflection)) &&
            write_text(staging_directory / "dependencies.txt", dependencies.str());
        if (!wrote_all)
        {
            std::filesystem::remove_all(staging_directory, error);
            add_error(result, request, "Failed to write all ShaderCodeEntry records.");
            return result;
        }
        std::filesystem::rename(staging_directory, final_directory, error);
        if (error)
        {
            const std::string rename_error = error.message();
            std::filesystem::remove_all(staging_directory, error);
            add_error(result, request, "Failed to publish ShaderCodeEntry atomically: " + rename_error);
            return result;
        }
        result.entry_directory = final_directory;
        return result;
    }
}
