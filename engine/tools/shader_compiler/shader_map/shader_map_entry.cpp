#include "shader_map/shader_map_entry.h"

#include <algorithm>
#include <sstream>
#include <type_traits>

#include "shader_map/shader_code_entry.h"
#include "shader_map/shader_map_storage.h"

namespace toy3d::shader
{
    namespace
    {
        template<typename T>
        void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned converted = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
                bytes.push_back(static_cast<std::uint8_t>(converted >> (index * 8u)));
        }

        void append_string(std::vector<std::uint8_t>& bytes, const std::string& value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        bool hash_is_zero(const Sha256Hash& hash)
        {
            return std::all_of(hash.begin(), hash.end(), [](std::uint8_t byte) { return byte == 0u; });
        }

        const char* stage_name(ShaderStageFlags stage)
        {
            switch (stage)
            {
            case ShaderStageFlags::Vertex: return "vertex";
            case ShaderStageFlags::Pixel: return "pixel";
            case ShaderStageFlags::Compute: return "compute";
            default: return "unknown";
            }
        }

        void add_error(ShaderMapEntryWriteResult& result, const std::string& message)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error,
                DiagnosticCode::ShaderCodeWriteFailed, {}, message});
        }

        Sha256Hash calculate_shader_map_key(const ShaderMapEntry& entry)
        {
            std::vector<std::uint8_t> bytes;
            append_integer(bytes, shader_map_entry_version);
            append_string(bytes, entry.shader_name);
            append_string(bytes, entry.pass_name);
            append_integer(bytes, static_cast<std::uint32_t>(entry.target));
            append_integer(bytes, static_cast<std::uint32_t>(entry.profile));
            append_integer(bytes, entry.mapping_version);
            bytes.insert(bytes.end(), entry.logical_layout_hash.begin(), entry.logical_layout_hash.end());
            bytes.insert(bytes.end(), entry.target_binding_hash.begin(), entry.target_binding_hash.end());
            bytes.insert(bytes.end(), entry.pass_template_hash.begin(), entry.pass_template_hash.end());
            for (const ShaderCodeEntry& stage : entry.stages)
            {
                append_integer(bytes, static_cast<std::uint32_t>(stage.request.stage));
                bytes.insert(bytes.end(), stage.request.compile_key.begin(), stage.request.compile_key.end());
                bytes.insert(bytes.end(), stage.reflection.reflection_hash.begin(),
                    stage.reflection.reflection_hash.end());
                const Sha256Hash binary_hash = sha256(stage.binary);
                bytes.insert(bytes.end(), binary_hash.begin(), binary_hash.end());
            }
            return sha256(bytes);
        }
    }

    bool ShaderMapEntryWriteResult::succeeded() const
    {
        return entry_directory.has_value() && diagnostics.empty();
    }

    ShaderMapEntryWriteResult write_verified_shader_map_entry(
        PlatformFile& platform_file,
        const PhysicalPath& shader_map_root,
        const ShaderMapEntry& entry)
    {
        ShaderMapEntryWriteResult result;
        if (shader_map_root.empty() || entry.shader_name.empty() || entry.pass_name.empty() ||
            entry.target != ShaderTarget::VulkanSpirV ||
            entry.profile != ShaderCompileProfile::VulkanPortableV1 ||
            entry.mapping_version != vulkan_binding_mapping_version || entry.stages.empty() ||
            hash_is_zero(entry.logical_layout_hash) || hash_is_zero(entry.target_binding_hash) ||
            hash_is_zero(entry.pass_template_hash))
        {
            add_error(result, "ShaderMapEntry publication requires a fully validated Vulkan Program.");
            return result;
        }
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            if (stage.binary.empty() || stage.request.stage != stage.reflection.stage ||
                stage.request.entry_point != stage.reflection.entry_point ||
                stage.request.logical_layout_hash != entry.logical_layout_hash ||
                stage.request.target_binding_hash != entry.target_binding_hash ||
                hash_is_zero(stage.request.compile_key) || hash_is_zero(stage.reflection.reflection_hash))
            {
                add_error(result, "ShaderMapEntry contains an invalid ShaderCodeEntry.");
                return result;
            }
        }

        result.shader_map_key = calculate_shader_map_key(entry);
        const std::string key = sha256_to_hex(result.shader_map_key);
        ShaderEntryStagingResult staging = create_shader_entry_staging_directory(
            platform_file, shader_map_root, key);
        if (!staging.succeeded())
        {
            add_error(result,
                "Failed to create ShaderMapEntry directory: " + staging.status.message);
            return result;
        }

        std::ostringstream manifest;
        manifest << "shader_map_entry_version=" << shader_map_entry_version << '\n'
                 << "shader_map_key=" << key << '\n'
                 << "shader_name=" << entry.shader_name << '\n'
                 << "pass_name=" << entry.pass_name << '\n'
                 << "target=" << static_cast<std::uint32_t>(entry.target) << '\n'
                 << "profile=" << static_cast<std::uint32_t>(entry.profile) << '\n'
                 << "mapping_version=" << entry.mapping_version << '\n'
                 << "logical_layout_hash=" << sha256_to_hex(entry.logical_layout_hash) << '\n'
                 << "target_binding_hash=" << sha256_to_hex(entry.target_binding_hash) << '\n'
                 << "pass_template_hash=" << sha256_to_hex(entry.pass_template_hash) << '\n'
                 << "stage_count=" << entry.stages.size() << '\n';
        std::ostringstream mapping;
        mapping << "mapping_version=" << entry.mapping_version << '\n';
        for (const ShaderMapBinding& binding : entry.bindings)
        {
            mapping << "binding=" << binding.binding_id << '\t' << binding.name << '\t'
                    << static_cast<std::uint32_t>(binding.group) << '\t'
                    << static_cast<std::uint32_t>(binding.category) << '\t'
                    << static_cast<std::uint32_t>(binding.stages) << '\t'
                    << static_cast<std::uint32_t>(binding.register_class) << '\t'
                    << binding.register_index << '\t' << binding.descriptor_set << '\t'
                    << binding.descriptor_binding << '\n';
        }

        const auto write_text = [&](const std::string& name, const std::string& text) {
            const FileResult<PhysicalPath> path =
                platform_file.join_relative(*staging.staging_directory, name);
            return path.succeeded()
                ? platform_file.write_text_utf8(path.value(), text, FileWriteMode::CreateNew)
                : path.status();
        };
        bool wrote_all = write_text("manifest.txt", manifest.str()).succeeded() &&
            write_text("mapping.txt", mapping.str()).succeeded();
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            const std::string prefix = stage_name(stage.request.stage);
            const Sha256Hash binary_hash = sha256(stage.binary);
            std::ostringstream stage_manifest;
            stage_manifest << "stage=" << static_cast<std::uint32_t>(stage.request.stage) << '\n'
                           << "entry_point=" << stage.request.entry_point << '\n'
                           << "compile_key=" << sha256_to_hex(stage.request.compile_key) << '\n'
                           << "reflection_hash=" << sha256_to_hex(stage.reflection.reflection_hash) << '\n'
                           << "binary_hash=" << sha256_to_hex(binary_hash) << '\n';
            std::ostringstream dependencies;
            for (const ShaderDependency& dependency : stage.request.dependencies)
                dependencies << dependency.virtual_path << '\t'
                             << sha256_to_hex(dependency.content_hash) << '\n';
            const FileResult<PhysicalPath> binary_path = platform_file.join_relative(
                *staging.staging_directory, prefix + ".spv");
            wrote_all = wrote_all &&
                write_text(prefix + ".manifest.txt", stage_manifest.str()).succeeded() &&
                binary_path.succeeded() &&
                platform_file.write_binary(
                    binary_path.value(), stage.binary, FileWriteMode::CreateNew).succeeded() &&
                write_text(prefix + ".reflection.txt",
                    serialize_shader_stage_reflection(stage.reflection)).succeeded() &&
                write_text(prefix + ".dependencies.txt", dependencies.str()).succeeded();
        }
        if (!wrote_all)
        {
            cleanup_shader_entry_staging_directory(platform_file, *staging.staging_directory);
            add_error(result, "Failed to write all ShaderMapEntry records.");
            return result;
        }
        const FileStatus published = publish_shader_entry_directory(
            platform_file, *staging.staging_directory, *staging.final_directory);
        if (!published.succeeded())
        {
            cleanup_shader_entry_staging_directory(platform_file, *staging.staging_directory);
            add_error(result,
                "Failed to publish ShaderMapEntry atomically: " + published.message);
            return result;
        }
        result.entry_directory = *staging.final_directory;
        return result;
    }
}
