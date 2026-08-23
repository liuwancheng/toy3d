#include "rendercore/shader/loaders/shader_map_entry_loader.h"

#include "format/sha256.h"
#include "format/shader_map_entry.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <utility>

namespace toy3d
{
    // Conversion helpers use optional to reject unsupported reflection values
    // and withhold incomplete ShaderMap program data from runtime callers.
    namespace
    {
        RHIShaderStage to_rhi_stage(shader::ShaderStageFlags stage)
        {
            switch (stage)
            {
            case shader::ShaderStageFlags::Vertex: return RHIShaderStage::Vertex;
            case shader::ShaderStageFlags::Pixel: return RHIShaderStage::Pixel;
            case shader::ShaderStageFlags::Compute: return RHIShaderStage::Compute;
            default: return RHIShaderStage::Vertex;
            }
        }

        RHIShaderStageFlags to_rhi_stage_flags(shader::ShaderStageFlags stages)
        {
            RHIShaderStageFlags result = RHIShaderStageFlags::None;
            if (shader::has_stage(stages, shader::ShaderStageFlags::Vertex))
                result = rhi_enum_or(result, RHIShaderStageFlags::Vertex);
            if (shader::has_stage(stages, shader::ShaderStageFlags::Pixel))
                result = rhi_enum_or(result, RHIShaderStageFlags::Pixel);
            if (shader::has_stage(stages, shader::ShaderStageFlags::Compute))
                result = rhi_enum_or(result, RHIShaderStageFlags::Compute);
            return result;
        }

        RHIBindingGroup to_rhi_group(shader::BindingGroup group)
        {
            switch (group)
            {
            case shader::BindingGroup::Global: return RHIBindingGroup::Global;
            case shader::BindingGroup::View: return RHIBindingGroup::View;
            case shader::BindingGroup::Pass: return RHIBindingGroup::Pass;
            case shader::BindingGroup::Material: return RHIBindingGroup::Material;
            case shader::BindingGroup::Object: return RHIBindingGroup::Object;
            }
            return RHIBindingGroup::Max;
        }

        std::optional<RHIResourceBindingType> to_rhi_type(
            shader::ShaderParameterCategory category)
        {
            switch (category)
            {
            case shader::ShaderParameterCategory::Constant:
                return RHIResourceBindingType::UniformBuffer;
            case shader::ShaderParameterCategory::SampledTexture:
                return RHIResourceBindingType::SampledTexture;
            case shader::ShaderParameterCategory::Sampler:
                return RHIResourceBindingType::Sampler;
            case shader::ShaderParameterCategory::ReadOnlyBuffer:
                return RHIResourceBindingType::ReadOnlyBuffer;
            case shader::ShaderParameterCategory::StorageBuffer:
                return RHIResourceBindingType::StorageBuffer;
            case shader::ShaderParameterCategory::StorageTexture:
                return RHIResourceBindingType::StorageTexture;
            }
            return std::nullopt;
        }

        ShaderValueType to_shader_value_type(shader::ShaderValueType type)
        {
            switch (type)
            {
            case shader::ShaderValueType::Float32: return ShaderValueType::Float32;
            case shader::ShaderValueType::Float32x2: return ShaderValueType::Float32x2;
            case shader::ShaderValueType::Float32x3: return ShaderValueType::Float32x3;
            case shader::ShaderValueType::Float32x4: return ShaderValueType::Float32x4;
            case shader::ShaderValueType::Int32: return ShaderValueType::Int32;
            case shader::ShaderValueType::Int32x2: return ShaderValueType::Int32x2;
            case shader::ShaderValueType::Int32x3: return ShaderValueType::Int32x3;
            case shader::ShaderValueType::Int32x4: return ShaderValueType::Int32x4;
            case shader::ShaderValueType::UInt32: return ShaderValueType::UInt32;
            case shader::ShaderValueType::UInt32x2: return ShaderValueType::UInt32x2;
            case shader::ShaderValueType::UInt32x3: return ShaderValueType::UInt32x3;
            case shader::ShaderValueType::UInt32x4: return ShaderValueType::UInt32x4;
            case shader::ShaderValueType::Float32x2x2: return ShaderValueType::Float32x2x2;
            case shader::ShaderValueType::Float32x2x3: return ShaderValueType::Float32x2x3;
            case shader::ShaderValueType::Float32x2x4: return ShaderValueType::Float32x2x4;
            case shader::ShaderValueType::Float32x3x2: return ShaderValueType::Float32x3x2;
            case shader::ShaderValueType::Float32x3x3: return ShaderValueType::Float32x3x3;
            case shader::ShaderValueType::Float32x3x4: return ShaderValueType::Float32x3x4;
            case shader::ShaderValueType::Float32x4x2: return ShaderValueType::Float32x4x2;
            case shader::ShaderValueType::Float32x4x3: return ShaderValueType::Float32x4x3;
            case shader::ShaderValueType::Float32x4x4: return ShaderValueType::Float32x4x4;
            }
            return ShaderValueType::Float32;
        }

        bool same_reflected_layout(
            const shader::ReflectedBinding& left,
            const shader::ReflectedBinding& right)
        {
            if (left.array_count != right.array_count ||
                left.constant_buffer_size != right.constant_buffer_size ||
                left.constant_members.size() != right.constant_members.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < left.constant_members.size(); ++index)
            {
                const shader::ReflectedConstantMember& a = left.constant_members[index];
                const shader::ReflectedConstantMember& b = right.constant_members[index];
                if (a.parameter_id != b.parameter_id || a.name != b.name || a.type != b.type ||
                    a.offset != b.offset || a.size != b.size ||
                    a.array_stride != b.array_stride || a.matrix_stride != b.matrix_stride)
                {
                    return false;
                }
            }
            return true;
        }

        ShaderMapBinding convert_binding(
            const shader::ShaderMapBinding& binding,
            const shader::ReflectedBinding& reflected)
        {
            ShaderMapBinding result;
            result.parameter_id = binding.binding_id;
            result.name = binding.name;
            result.group = to_rhi_group(binding.group);
            result.type = *to_rhi_type(binding.category);
            result.stages = to_rhi_stage_flags(binding.stages);
            result.target_binding = binding.descriptor_binding;
            result.array_count = reflected.array_count;
            result.constant_buffer_size = reflected.constant_buffer_size;
            for (const shader::ReflectedConstantMember& member : reflected.constant_members)
            {
                result.constant_members.push_back({member.parameter_id, member.name,
                    to_shader_value_type(member.type), member.offset, member.size,
                    member.array_stride, member.matrix_stride});
            }
            return result;
        }

        std::optional<ShaderMapProgramData> convert_entry(
            const shader::ShaderMapEntry& entry,
            std::string& error)
        {
            if (entry.target != shader::ShaderTarget::VulkanSpirV ||
                entry.profile != shader::ShaderCompileProfile::VulkanPortableV1)
            {
                error = "ShaderMapEntry loader only supports VulkanPortable v1 entries.";
                return std::nullopt;
            }
            ShaderMapProgramData program;
            program.shader_name = entry.shader_name;
            program.pass_name = entry.pass_name;
            program.platform = ShaderPlatform::VulkanPortableV1;
            program.mapping_version = entry.mapping_version;
            program.logical_layout_hash = entry.logical_layout_hash;
            program.target_binding_hash = entry.target_binding_hash;
            program.pass_template_hash = entry.pass_template_hash;
            program.permutation_key = entry.permutation_key;

            for (const shader::ShaderMapBinding& binding : entry.bindings)
            {
                const auto type = to_rhi_type(binding.category);
                if (!type)
                {
                    error = "ShaderMap entry contains an unsupported binding category.";
                    return std::nullopt;
                }
                if (to_rhi_group(binding.group) == RHIBindingGroup::Max)
                {
                    error = "ShaderMap entry contains an unsupported binding group.";
                    return std::nullopt;
                }
                const shader::ReflectedBinding* reflected_layout = nullptr;
                for (const shader::ShaderCodeEntry& stage : entry.stages)
                {
                    const auto reflected = std::find_if(
                        stage.reflection.bindings.begin(), stage.reflection.bindings.end(),
                        [&](const shader::ReflectedBinding& value) {
                            return value.parameter_id == binding.binding_id;
                        });
                    if (reflected == stage.reflection.bindings.end()) continue;
                    if (reflected_layout && !same_reflected_layout(*reflected_layout, *reflected))
                    {
                        error = "ShaderMap entry has inconsistent reflected binding metadata.";
                        return std::nullopt;
                    }
                    reflected_layout = &*reflected;
                }
                if (!reflected_layout)
                {
                    error = "ShaderMap entry binding is absent from stage reflection.";
                    return std::nullopt;
                }
                program.bindings.push_back(convert_binding(binding, *reflected_layout));
            }

            for (const shader::ShaderCodeEntry& stage : entry.stages)
            {
                ShaderMapStage output;
                output.stage = to_rhi_stage(stage.request.stage);
                output.entry_point = stage.request.entry_point;
                output.binary = stage.binary;
                output.content_hash = shader::sha256(stage.binary);
                for (const shader::ReflectedBinding& reflected : stage.reflection.bindings)
                {
                    const auto mapping = std::find_if(entry.bindings.begin(), entry.bindings.end(),
                        [&](const shader::ShaderMapBinding& binding) {
                            return binding.binding_id == reflected.parameter_id;
                        });
                    if (mapping == entry.bindings.end() || !to_rhi_type(mapping->category))
                    {
                        error = "ShaderMap reflection has no supported target mapping.";
                        return std::nullopt;
                    }
                    output.reflection.push_back(convert_binding(*mapping, reflected));
                }
                output.interface_variables = stage.reflection.interface_variables;
                if (output.stage == RHIShaderStage::Vertex)
                {
                    std::set<ShaderVertexAttributeId> attributes;
                    for (const shader::ReflectedInterfaceVariable& reflected :
                         output.interface_variables)
                    {
                        if (!reflected.input)
                        {
                            continue;
                        }
                        ShaderVertexInput vertex_input;
                        if (!try_make_shader_vertex_input(
                                reflected, vertex_input, error))
                        {
                            return std::nullopt;
                        }
                        if (!attributes.insert(vertex_input.attribute_id).second)
                        {
                            error = "ShaderMap vertex inputs contain a duplicate logical attribute.";
                            return std::nullopt;
                        }
                        program.vertex_inputs.push_back(
                            std::move(vertex_input));
                    }
                }
                program.stages.push_back(std::move(output));
            }
            return program;
        }

        std::string diagnostics_text(
            const std::vector<std::string>& diagnostics)
        {
            std::ostringstream stream;
            for (std::size_t index = 0; index < diagnostics.size(); ++index)
            {
                if (index != 0) stream << ' ';
                stream << diagnostics[index];
            }
            return stream.str();
        }
    }

    ShaderMapEntryLoader::ShaderMapEntryLoader(PhysicalPath entry_root)
        : entry_root_(std::move(entry_root))
    {
    }

    ShaderMapProgramLoadResult ShaderMapEntryLoader::load_program(
        const ShaderMapProgramKey& key) const
    {
        ShaderMapProgramLoadResult result;
        if (key.platform != ShaderPlatform::VulkanPortableV1)
        {
            result.error = "ShaderMapEntry loader does not support the requested ShaderPlatform.";
            return result;
        }
        const auto entries = platform_file_.enumerate_directory(entry_root_);
        if (!entries.succeeded())
        {
            result.error = "Unable to enumerate ShaderMapEntry root: " +
                entries.status().message;
            return result;
        }

        for (const DirectoryEntry& directory : entries.value())
        {
            if (directory.type != FileType::Directory) continue;
            // filesystem extracts the final host directory component for cache
            // discovery without duplicating platform separator rules.
            const std::string name = std::filesystem::path(directory.path.utf8()).filename().string();
            const auto entry_key = shader::sha256_from_hex(name);
            if (!entry_key) continue;
            shader::ShaderMapEntryReadResult read = shader::read_verified_shader_map_entry(
                platform_file_, entry_root_, *entry_key);
            if (!read.succeeded())
            {
                result.error = "ShaderMapEntry verification failed: " +
                    diagnostics_text(read.diagnostics);
                return result;
            }
            if (read.entry->shader_name != key.shader_name ||
                read.entry->pass_name != key.pass_name ||
                read.entry->permutation_key != key.permutation_key)
            {
                continue;
            }
            std::string conversion_error;
            auto converted = convert_entry(*read.entry, conversion_error);
            if (!converted)
            {
                result.error = std::move(conversion_error);
                return result;
            }
            ShaderMapProgramLoadResult validated = validate_shader_map_program(
                std::move(*converted), key);
            if (!validated.succeeded())
            {
                result.error = std::move(validated.error);
                return result;
            }
            if (result.program)
            {
                result.program.reset();
                result.error = "ShaderMapEntry lookup returned duplicate Program identities.";
                return result;
            }
            result.program = std::move(validated.program);
        }
        if (!result.program)
        {
            result.error = "ShaderMapEntry storage does not contain the requested Program.";
        }
        return result;
    }
}
