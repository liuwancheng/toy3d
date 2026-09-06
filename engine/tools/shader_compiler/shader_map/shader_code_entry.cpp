#include "shader_map/shader_code_entry.h"

#include <sstream>

#include "shader_map/shader_map_storage.h"

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
        PlatformFile& platform_file,
        const PhysicalPath& entry_root,
        const ShaderCompileRequest& request,
        const TargetBindingLayout& target_layout,
        const ShaderStageReflection& reflection,
        const std::vector<std::uint8_t>& binary)
    {
        ShaderCodeEntryWriteResult result;
        if (entry_root.empty() || binary.empty() || request.target != ShaderTarget::VulkanSpirV ||
            request.profile != ShaderCompileProfile::VulkanES31 ||
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
        ShaderEntryStagingResult staging = create_shader_entry_staging_directory(
            platform_file, entry_root, key);
        if (!staging.succeeded())
        {
            add_error(result, request,
                "Failed to create ShaderCodeEntry staging directory: " + staging.status.message);
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

        const auto write_text = [&](const std::string& name, const std::string& text) {
            const FileResult<PhysicalPath> path =
                platform_file.join_relative(*staging.staging_directory, name);
            return path.succeeded()
                ? platform_file.write_text_utf8(path.value(), text, FileWriteMode::CreateNew)
                : path.status();
        };
        const FileResult<PhysicalPath> binary_path =
            platform_file.join_relative(*staging.staging_directory, "shader.spv");
        const FileStatus manifest_status = write_text("manifest.txt", manifest.str());
        const FileStatus binary_status = binary_path.succeeded()
            ? platform_file.write_binary(binary_path.value(), binary, FileWriteMode::CreateNew)
            : binary_path.status();
        const FileStatus reflection_status = write_text(
            "reflection.txt", serialize_shader_stage_reflection(reflection));
        const FileStatus dependencies_status = write_text(
            "dependencies.txt", dependencies.str());
        if (!manifest_status.succeeded() || !binary_status.succeeded() ||
            !reflection_status.succeeded() || !dependencies_status.succeeded())
        {
            cleanup_shader_entry_staging_directory(platform_file, *staging.staging_directory);
            add_error(result, request, "Failed to write all ShaderCodeEntry records.");
            return result;
        }
        const FileStatus published = publish_shader_entry_directory(
            platform_file, *staging.staging_directory, *staging.final_directory);
        if (!published.succeeded())
        {
            cleanup_shader_entry_staging_directory(platform_file, *staging.staging_directory);
            add_error(result, request,
                "Failed to publish ShaderCodeEntry atomically: " + published.message);
            return result;
        }
        result.entry_directory = *staging.final_directory;
        return result;
    }
}
