#include "shader_map/shader_map_entry.h"

#include <algorithm>
#include <set>
#include <sstream>

#include "file_system/virtual_path.h"
#include "shader_map/shader_code_entry.h"
#include "shader_map/shader_map_storage.h"

namespace toy3d::shader
{
    namespace
    {
        bool hash_is_zero(const Sha256Hash& hash)
        {
            return std::all_of(hash.begin(), hash.end(), [](std::uint8_t byte) { return byte == 0u; });
        }

        const char* stage_name(ShaderStageFlags stage)
        {
            switch (stage)
            {
            case ShaderStageFlags::Vertex:
                return "vertex";
            case ShaderStageFlags::Pixel:
                return "pixel";
            case ShaderStageFlags::Compute:
                return "compute";
            default:
                return "unknown";
            }
        }

        void add_error(ShaderMapEntryWriteResult& result, const std::string& message)
        {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::ShaderCodeWriteFailed, {}, message});
        }

        bool accept_existing_cache_hit(ShaderMapEntryWriteResult& result, const PlatformFile& platform_file,
                                       const PhysicalPath& shader_map_root)
        {
            ShaderMapEntryReadResult existing =
                read_verified_shader_map_entry(platform_file, shader_map_root, result.shader_map_key);
            if (!existing.succeeded())
            {
                for (std::string& message : existing.diagnostics)
                {
                    result.diagnostics.push_back(
                        {DiagnosticSeverity::Error, DiagnosticCode::ShaderMapReadFailed, {}, std::move(message)});
                }
                return false;
            }
            if (existing.entry_content_hash != result.entry_content_hash)
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error,
                                              DiagnosticCode::ShaderMapCacheConflict,
                                              {},
                                              "Existing ShaderMapEntry key has different validated content."});
                return false;
            }
            result.entry_directory = *existing.entry_directory;
            result.cache_hit = true;
            return true;
        }

        bool validate_dependencies(const std::vector<ShaderDependency>& dependencies)
        {
            std::string previous;
            for (const ShaderDependency& dependency : dependencies)
            {
                const FileResult<VirtualPath> path = VirtualPath::parse(dependency.virtual_path);
                if (!path.succeeded() || path.value().utf8() != dependency.virtual_path ||
                    hash_is_zero(dependency.content_hash) || (!previous.empty() && previous >= dependency.virtual_path))
                    return false;
                previous = dependency.virtual_path;
            }
            return true;
        }
    } // namespace

    bool ShaderMapEntryWriteResult::succeeded() const
    {
        return entry_directory.has_value() && diagnostics.empty();
    }

    ShaderMapEntryWriteResult write_verified_shader_map_entry(PlatformFile& platform_file,
                                                              const PhysicalPath& shader_map_root,
                                                              const ShaderMapEntry& entry)
    {
        ShaderMapEntryWriteResult result;
        if (shader_map_root.empty() || entry.shader_name.empty() || entry.pass_name.empty() ||
            entry.target != ShaderTarget::VulkanSpirV || entry.profile != ShaderCompileProfile::VulkanES31 ||
            entry.mapping_version != vulkan_binding_mapping_version || entry.stages.empty() ||
            hash_is_zero(entry.logical_layout_hash) || hash_is_zero(entry.target_binding_hash) ||
            hash_is_zero(entry.pass_template_hash) || !is_valid_shader_graphics_pass_state(entry.graphics_pass_state) ||
            calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state) != entry.pass_template_hash ||
            entry.variant_id_version != shader_variant_id_version ||
            entry.permutation_version != shader_permutation_version || hash_is_zero(entry.permutation_key))
        {
            add_error(result, "ShaderMapEntry publication requires a fully validated Vulkan Program.");
            return result;
        }
        std::uint32_t stage_mask = 0;
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            const std::uint32_t stage_value = static_cast<std::uint32_t>(stage.request.stage);
            std::set<ShaderParameterId> reflected_ids;
            std::set<std::pair<std::uint32_t, std::uint32_t>> reflected_slots;
            const bool unique_reflection = std::all_of(
                stage.reflection.bindings.begin(), stage.reflection.bindings.end(),
                [&](const ReflectedBinding& binding)
                {
                    return reflected_ids.insert(binding.parameter_id).second &&
                           reflected_slots.emplace(binding.descriptor_set, binding.descriptor_binding).second;
                });
            if (stage.binary.empty() || stage.request.stage != stage.reflection.stage || !unique_reflection ||
                (stage_value != static_cast<std::uint32_t>(ShaderStageFlags::Vertex) &&
                 stage_value != static_cast<std::uint32_t>(ShaderStageFlags::Pixel) &&
                 stage_value != static_cast<std::uint32_t>(ShaderStageFlags::Compute)) ||
                (stage_mask & stage_value) != 0u || stage.request.entry_point != stage.reflection.entry_point ||
                stage.request.logical_layout_hash != entry.logical_layout_hash ||
                stage.request.target_binding_hash != entry.target_binding_hash ||
                hash_is_zero(stage.request.compile_key) || hash_is_zero(stage.reflection.reflection_hash) ||
                calculate_shader_stage_reflection_hash(stage.reflection) != stage.reflection.reflection_hash ||
                !validate_dependencies(stage.request.dependencies))
            {
                add_error(result, "ShaderMapEntry contains an invalid ShaderCodeEntry.");
                return result;
            }
            stage_mask |= stage_value;
        }
        const std::uint32_t graphics_mask =
            static_cast<std::uint32_t>(ShaderStageFlags::Vertex) | static_cast<std::uint32_t>(ShaderStageFlags::Pixel);
        if (stage_mask != static_cast<std::uint32_t>(ShaderStageFlags::Vertex) && stage_mask != graphics_mask &&
            stage_mask != static_cast<std::uint32_t>(ShaderStageFlags::Compute))
        {
            add_error(result, "ShaderMapEntry contains an invalid Program stage set.");
            return result;
        }
        TargetBindingLayout stored_layout;
        stored_layout.target = entry.target;
        stored_layout.mapping_version = entry.mapping_version;
        std::set<std::pair<std::uint32_t, std::uint32_t>> native_slots;
        std::set<ShaderParameterId> binding_ids;
        for (const ShaderMapBinding& binding : entry.bindings)
        {
            const bool is_constant = binding.category == ShaderParameterCategory::Constant;
            if (binding.binding_id == 0u || binding.name.empty() || static_cast<std::uint32_t>(binding.stages) == 0u ||
                (static_cast<std::uint32_t>(binding.stages) & ~stage_mask) != 0u || binding.descriptor_set > 3u ||
                (is_constant && (binding.data_size == 0u || hash_is_zero(binding.data_layout_hash) ||
                                 binding.shader_abi_version != toy_shader_abi_version)) ||
                (!is_constant && (binding.data_size != 0u || !hash_is_zero(binding.data_layout_hash) ||
                                  binding.shader_abi_version != 0u)) ||
                !binding_ids.insert(binding.binding_id).second ||
                !native_slots.emplace(binding.descriptor_set, binding.descriptor_binding).second)
            {
                add_error(result, "ShaderMapEntry contains an invalid mapping record.");
                return result;
            }
            stored_layout.bindings.push_back(
                {binding.binding_id, binding.name, binding.group, binding.category, binding.stages,
                 binding.register_class, binding.register_index, binding.descriptor_set, binding.descriptor_binding,
                 binding.data_size, binding.data_layout_hash, binding.shader_abi_version, nullptr});
        }
        if (calculate_target_binding_hash(stored_layout) != entry.target_binding_hash)
        {
            add_error(result, "ShaderMapEntry mapping does not match target_binding_hash.");
            return result;
        }
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            for (const ReflectedBinding& reflected : stage.reflection.bindings)
            {
                const auto mapping =
                    std::find_if(entry.bindings.begin(), entry.bindings.end(), [&](const ShaderMapBinding& binding)
                                 { return binding.binding_id == reflected.parameter_id; });
                if (mapping == entry.bindings.end() || mapping->name != reflected.name ||
                    mapping->group != reflected.group || mapping->category != reflected.category ||
                    !has_stage(mapping->stages, stage.request.stage) ||
                    mapping->descriptor_set != reflected.descriptor_set ||
                    mapping->descriptor_binding != reflected.descriptor_binding ||
                    mapping->data_size != reflected.constant_buffer_size ||
                    mapping->data_layout_hash != reflected.data_layout_hash ||
                    mapping->shader_abi_version != reflected.shader_abi_version)
                {
                    add_error(result, "ShaderMapEntry reflection does not match its mapping.");
                    return result;
                }
                if (reflected.category == ShaderParameterCategory::Constant &&
                    calculate_constant_buffer_data_layout_hash(
                        reflected.group, reflected.parameter_id, reflected.constant_buffer_size,
                        reflected.constant_members, reflected.shader_abi_version) != reflected.data_layout_hash)
                {
                    add_error(result, "ShaderMapEntry constant data layout hash is inconsistent.");
                    return result;
                }
            }
            for (const ShaderMapBinding& mapping : entry.bindings)
            {
                if (!has_stage(mapping.stages, stage.request.stage))
                    continue;
                const bool found = std::any_of(stage.reflection.bindings.begin(), stage.reflection.bindings.end(),
                                               [&](const ReflectedBinding& reflected)
                                               { return reflected.parameter_id == mapping.binding_id; });
                if (!found)
                {
                    add_error(result, "ShaderMapEntry mapping is missing from stage reflection.");
                    return result;
                }
            }
        }

        result.shader_map_key = calculate_shader_map_key(entry);
        result.entry_content_hash = calculate_shader_map_entry_content_hash(entry);
        const std::string key = sha256_to_hex(result.shader_map_key);
        ShaderEntryStagingResult staging = create_shader_entry_staging_directory(platform_file, shader_map_root, key);
        if (!staging.succeeded())
        {
            if (staging.status.code == FileErrorCode::AlreadyExists &&
                accept_existing_cache_hit(result, platform_file, shader_map_root))
                return result;
            add_error(result, "Failed to create ShaderMapEntry directory: " + staging.status.message);
            return result;
        }

        std::ostringstream manifest;
        manifest << "shader_map_entry_version=" << shader_map_entry_version << '\n'
                 << "shader_map_key=" << key << '\n'
                 << "entry_content_hash=" << sha256_to_hex(result.entry_content_hash) << '\n'
                 << "shader_name=" << entry.shader_name << '\n'
                 << "pass_name=" << entry.pass_name << '\n'
                 << "target=" << static_cast<std::uint32_t>(entry.target) << '\n'
                 << "profile=" << static_cast<std::uint32_t>(entry.profile) << '\n'
                 << "mapping_version=" << entry.mapping_version << '\n'
                 << "logical_layout_hash=" << sha256_to_hex(entry.logical_layout_hash) << '\n'
                 << "target_binding_hash=" << sha256_to_hex(entry.target_binding_hash) << '\n'
                 << "pass_template_hash=" << sha256_to_hex(entry.pass_template_hash) << '\n'
                 << "pass_primitive_topology="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.primitive_topology) << '\n'
                 << "pass_cull_mode=" << static_cast<std::uint32_t>(entry.graphics_pass_state.cull_mode) << '\n'
                 << "pass_front_face=" << static_cast<std::uint32_t>(entry.graphics_pass_state.front_face) << '\n'
                 << "pass_fill_mode=" << static_cast<std::uint32_t>(entry.graphics_pass_state.fill_mode) << '\n'
                 << "pass_depth_test_enable=" << (entry.graphics_pass_state.depth_test_enable ? 1u : 0u) << '\n'
                 << "pass_depth_compare_operation="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.depth_compare_operation) << '\n'
                 << "pass_depth_write_enable=" << (entry.graphics_pass_state.depth_write_enable ? 1u : 0u) << '\n'
                 << "pass_stencil_mode=" << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.mode) << '\n'
                 << "pass_stencil_read_mask=" << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.read_mask)
                 << '\n'
                 << "pass_stencil_write_mask="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.write_mask) << '\n'
                 << "pass_stencil_front_compare="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.front.compare_operation) << '\n'
                 << "pass_stencil_front_fail="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.front.fail_operation) << '\n'
                 << "pass_stencil_front_depth_fail="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.front.depth_fail_operation) << '\n'
                 << "pass_stencil_front_pass="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.front.pass_operation) << '\n'
                 << "pass_stencil_back_compare="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.back.compare_operation) << '\n'
                 << "pass_stencil_back_fail="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.back.fail_operation) << '\n'
                 << "pass_stencil_back_depth_fail="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.back.depth_fail_operation) << '\n'
                 << "pass_stencil_back_pass="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.stencil.back.pass_operation) << '\n'
                 << "pass_blend_enable=" << (entry.graphics_pass_state.blend.enabled ? 1u : 0u) << '\n'
                 << "pass_source_color_factor="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.blend.source_color_factor) << '\n'
                 << "pass_destination_color_factor="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.blend.destination_color_factor) << '\n'
                 << "pass_color_blend_operation="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.blend.color_operation) << '\n'
                 << "pass_source_alpha_factor="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.blend.source_alpha_factor) << '\n'
                 << "pass_destination_alpha_factor="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.blend.destination_alpha_factor) << '\n'
                 << "pass_alpha_blend_operation="
                 << static_cast<std::uint32_t>(entry.graphics_pass_state.blend.alpha_operation) << '\n'
                 << "pass_color_write_mask=" << static_cast<std::uint32_t>(entry.graphics_pass_state.color_write_mask)
                 << '\n'
                 << "variant_id_version=" << entry.variant_id_version << '\n'
                 << "permutation_version=" << entry.permutation_version << '\n'
                 << "permutation_key=" << sha256_to_hex(entry.permutation_key) << '\n'
                 << "stage_count=" << entry.stages.size() << '\n';
        std::ostringstream mapping;
        mapping << "mapping_version=" << entry.mapping_version << '\n';
        for (const ShaderMapBinding& binding : entry.bindings)
        {
            mapping << "binding=" << binding.binding_id << '\t' << binding.name << '\t'
                    << static_cast<std::uint32_t>(binding.group) << '\t' << static_cast<std::uint32_t>(binding.category)
                    << '\t' << static_cast<std::uint32_t>(binding.stages) << '\t'
                    << static_cast<std::uint32_t>(binding.register_class) << '\t' << binding.register_index << '\t'
                    << binding.descriptor_set << '\t' << binding.descriptor_binding << '\t' << binding.data_size
                    << '\t' << sha256_to_hex(binding.data_layout_hash) << '\t' << binding.shader_abi_version << '\n';
        }

        const auto write_text = [&](const std::string& name, const std::string& text)
        {
            const FileResult<PhysicalPath> path = platform_file.join_relative(*staging.staging_directory, name);
            return path.succeeded() ? platform_file.write_text_utf8(path.value(), text, FileWriteMode::CreateNew)
                                    : path.status();
        };
        bool wrote_all = write_text("manifest.txt", manifest.str()).succeeded() &&
                         write_text("mapping.txt", mapping.str()).succeeded();
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            const std::string prefix = stage_name(stage.request.stage);
            const Sha256Hash binary_hash = sha256(stage.binary);
            const std::string reflection_text = serialize_shader_stage_reflection(stage.reflection);
            std::ostringstream dependencies;
            for (const ShaderDependency& dependency : stage.request.dependencies)
                dependencies << dependency.virtual_path << '\t' << sha256_to_hex(dependency.content_hash) << '\n';
            const std::string dependencies_text = dependencies.str();
            std::ostringstream stage_manifest;
            stage_manifest << "stage=" << static_cast<std::uint32_t>(stage.request.stage) << '\n'
                           << "entry_point=" << stage.request.entry_point << '\n'
                           << "compile_key=" << sha256_to_hex(stage.request.compile_key) << '\n'
                           << "reflection_hash=" << sha256_to_hex(stage.reflection.reflection_hash) << '\n'
                           << "binary_hash=" << sha256_to_hex(binary_hash) << '\n'
                           << "reflection_file_hash=" << sha256_to_hex(sha256(reflection_text)) << '\n'
                           << "dependencies_file_hash=" << sha256_to_hex(sha256(dependencies_text)) << '\n';
            const FileResult<PhysicalPath> binary_path =
                platform_file.join_relative(*staging.staging_directory, prefix + ".spv");
            wrote_all =
                wrote_all && write_text(prefix + ".manifest.txt", stage_manifest.str()).succeeded() &&
                binary_path.succeeded() &&
                platform_file.write_binary(binary_path.value(), stage.binary, FileWriteMode::CreateNew).succeeded() &&
                write_text(prefix + ".reflection.txt", reflection_text).succeeded() &&
                write_text(prefix + ".dependencies.txt", dependencies_text).succeeded();
        }
        if (!wrote_all)
        {
            cleanup_shader_entry_staging_directory(platform_file, *staging.staging_directory);
            add_error(result, "Failed to write all ShaderMapEntry records.");
            return result;
        }
        const FileStatus published =
            publish_shader_entry_directory(platform_file, *staging.staging_directory, *staging.final_directory);
        if (!published.succeeded())
        {
            cleanup_shader_entry_staging_directory(platform_file, *staging.staging_directory);
            if (published.code == FileErrorCode::AlreadyExists &&
                accept_existing_cache_hit(result, platform_file, shader_map_root))
                return result;
            add_error(result, "Failed to publish ShaderMapEntry atomically: " + published.message);
            return result;
        }
        result.entry_directory = *staging.final_directory;
        return result;
    }
} // namespace toy3d::shader
