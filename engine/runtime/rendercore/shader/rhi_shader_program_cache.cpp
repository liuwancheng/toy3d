#include "rendercore/shader/rhi_shader_program_cache.h"

#include <functional>
#include <type_traits>
#include <utility>

namespace toy3d
{
    namespace
    {
        void hash_combine(std::size_t& seed, std::size_t value)
        {
            seed ^= value + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        }

        template <typename T> void hash_scalar(std::size_t& seed, T value)
        {
            hash_combine(seed, std::hash<T>{}(value));
        }

        template <typename T> void hash_enum(std::size_t& seed, T value)
        {
            using Underlying = typename std::underlying_type<T>::type;
            hash_scalar(seed, static_cast<Underlying>(value));
        }

        void hash_content(std::size_t& seed, const ShaderContentHash& content)
        {
            for (std::uint8_t byte : content)
            {
                hash_scalar(seed, byte);
            }
        }
    } // namespace

    bool RHIShaderProgramKey::Binding::operator==(const Binding& other) const
    {
        return parameter_id == other.parameter_id && group == other.group && type == other.type &&
               stages == other.stages && target_binding == other.target_binding && array_count == other.array_count &&
               constant_buffer_size == other.constant_buffer_size && data_layout_hash == other.data_layout_hash &&
               shader_abi_version == other.shader_abi_version;
    }

    bool RHIShaderProgramKey::StageBinding::operator==(const StageBinding& other) const
    {
        return parameter_id == other.parameter_id && group == other.group && type == other.type &&
               target_binding == other.target_binding && array_count == other.array_count &&
               data_size == other.data_size && data_layout_hash == other.data_layout_hash &&
               shader_abi_version == other.shader_abi_version;
    }

    bool RHIShaderProgramKey::Stage::operator==(const Stage& other) const
    {
        return stage == other.stage && entry_point == other.entry_point && content_hash == other.content_hash &&
               reflection == other.reflection;
    }

    bool RHIShaderProgramKey::VertexInput::operator==(const VertexInput& other) const
    {
        return semantic_name == other.semantic_name && semantic_index == other.semantic_index &&
               target_location == other.target_location && scalar_type == other.scalar_type &&
               component_count == other.component_count;
    }

    RHIShaderProgramKey RHIShaderProgramKey::from_program(const ShaderMapProgramData& program)
    {
        RHIShaderProgramKey key;
        key.shader_name = program.shader_name;
        key.pass_name = program.pass_name;
        key.platform = program.platform;
        key.permutation_key = program.permutation_key;
        key.mapping_version = program.mapping_version;
        key.logical_layout_hash = program.logical_layout_hash;
        key.target_binding_hash = program.target_binding_hash;
        key.bindings.reserve(program.bindings.size());
        for (const ShaderMapBinding& source : program.bindings)
        {
            Binding binding;
            binding.parameter_id = source.parameter_id;
            binding.group = source.group;
            binding.type = source.type;
            binding.stages = source.stages;
            binding.target_binding = source.target_binding;
            binding.array_count = source.array_count;
            binding.constant_buffer_size = source.constant_buffer_size;
            binding.data_layout_hash = source.data_layout_hash;
            binding.shader_abi_version = source.shader_abi_version;
            key.bindings.push_back(binding);
        }
        key.stages.reserve(program.stages.size());
        for (const ShaderMapStage& source : program.stages)
        {
            Stage stage;
            stage.stage = source.stage;
            stage.entry_point = source.entry_point;
            stage.content_hash = source.content_hash;
            stage.reflection.reserve(source.reflection.size());
            for (const ShaderMapBinding& source_binding : source.reflection)
            {
                StageBinding binding;
                binding.parameter_id = source_binding.parameter_id;
                binding.group = source_binding.group;
                binding.type = source_binding.type;
                binding.target_binding = source_binding.target_binding;
                binding.array_count = source_binding.array_count;
                binding.data_size = source_binding.constant_buffer_size;
                binding.data_layout_hash = source_binding.data_layout_hash;
                binding.shader_abi_version = source_binding.shader_abi_version;
                stage.reflection.push_back(binding);
            }
            key.stages.push_back(std::move(stage));
        }
        key.vertex_inputs.reserve(program.vertex_inputs.size());
        for (const ShaderVertexInput& source : program.vertex_inputs)
        {
            VertexInput input;
            input.semantic_name = source.semantic_name;
            input.semantic_index = source.semantic_index;
            input.target_location = source.target_location;
            input.scalar_type = source.scalar_type;
            input.component_count = source.component_count;
            key.vertex_inputs.push_back(std::move(input));
        }
        return key;
    }

    bool RHIShaderProgramKey::operator==(const RHIShaderProgramKey& other) const
    {
        return shader_name == other.shader_name && pass_name == other.pass_name && platform == other.platform &&
               permutation_key == other.permutation_key && mapping_version == other.mapping_version &&
               logical_layout_hash == other.logical_layout_hash && target_binding_hash == other.target_binding_hash &&
               bindings == other.bindings && stages == other.stages && vertex_inputs == other.vertex_inputs;
    }

    std::size_t RHIShaderProgramKeyHash::operator()(const RHIShaderProgramKey& key) const
    {
        std::size_t result = 0;
        hash_scalar(result, key.shader_name);
        hash_scalar(result, key.pass_name);
        hash_enum(result, key.platform);
        hash_content(result, key.permutation_key);
        hash_scalar(result, key.mapping_version);
        hash_content(result, key.logical_layout_hash);
        hash_content(result, key.target_binding_hash);
        for (const RHIShaderProgramKey::Binding& binding : key.bindings)
        {
            hash_scalar(result, binding.parameter_id);
            hash_enum(result, binding.group);
            hash_enum(result, binding.type);
            hash_enum(result, binding.stages);
            hash_scalar(result, binding.target_binding);
            hash_scalar(result, binding.array_count);
            hash_scalar(result, binding.constant_buffer_size);
            hash_content(result, binding.data_layout_hash);
            hash_scalar(result, binding.shader_abi_version);
        }
        for (const RHIShaderProgramKey::Stage& stage : key.stages)
        {
            hash_enum(result, stage.stage);
            hash_scalar(result, stage.entry_point);
            hash_content(result, stage.content_hash);
            for (const RHIShaderProgramKey::StageBinding& binding : stage.reflection)
            {
                hash_scalar(result, binding.parameter_id);
                hash_enum(result, binding.group);
                hash_enum(result, binding.type);
                hash_scalar(result, binding.target_binding);
                hash_scalar(result, binding.array_count);
                hash_scalar(result, binding.data_size);
                hash_content(result, binding.data_layout_hash);
                hash_scalar(result, binding.shader_abi_version);
            }
        }
        for (const RHIShaderProgramKey::VertexInput& input : key.vertex_inputs)
        {
            hash_scalar(result, input.semantic_name);
            hash_scalar(result, input.semantic_index);
            hash_scalar(result, input.target_location);
            hash_enum(result, input.scalar_type);
            hash_scalar(result, input.component_count);
        }
        return result;
    }

    RHIShaderProgramCache::RHIShaderProgramCache(RHIDevice& device) : device_(device) {}

    RHIResult<RHIShaderProgramRef> RHIShaderProgramCache::find_or_create(const ShaderMapProgramRef& program)
    {
        if (!program)
        {
            return RHIResult<RHIShaderProgramRef>::failure(RHIErrorCode::InvalidArgument,
                                                           "RHI Shader Program cache requires a non-null CPU Program.");
        }

        RHIShaderProgramKey key = RHIShaderProgramKey::from_program(program->data());
        const auto found = programs_.find(key);
        if (found != programs_.end())
        {
            return RHIResult<RHIShaderProgramRef>::success(found->second);
        }

        RHIResult<RHIShaderProgramDesc> built = build_rhi_shader_program_desc(*program);
        if (!built)
        {
            return RHIResult<RHIShaderProgramRef>::failure(
                built.status().code(), "RHI Shader Program descriptor build failed for " + program->data().shader_name +
                                           "/" + program->data().pass_name + ": " + built.status().message());
        }
        RHIShaderProgramDesc desc = std::move(built).value();
        RHIShaderProgram candidate;

        RHIResult<RHIBindingLayoutRef> layout = device_.create_binding_layout(desc.binding_layout);
        if (!layout)
        {
            return RHIResult<RHIShaderProgramRef>::failure(
                layout.status().code(), "RHI Shader Program binding layout creation failed for " +
                                            program->data().shader_name + "/" + program->data().pass_name + ": " +
                                            layout.status().message());
        }
        candidate.binding_layout = std::move(layout).value();

        const auto create_stage = [this, &program](const std::optional<RHIShaderDesc>& shader_desc,
                                                   const char* stage_name, RHIShaderRef& output) -> RHIStatus
        {
            if (!shader_desc)
            {
                return RHIStatus::success();
            }
            RHIResult<RHIShaderRef> created = device_.create_shader(*shader_desc);
            if (!created)
            {
                return RHIStatus::failure(created.status().code(),
                                          "RHI Shader Program " + std::string(stage_name) +
                                              " stage creation failed for " + program->data().shader_name + "/" +
                                              program->data().pass_name + ": " + created.status().message());
            }
            output = std::move(created).value();
            return RHIStatus::success();
        };

        RHIStatus status = create_stage(desc.vertex_shader, "vertex", candidate.vertex_shader);
        if (status)
        {
            status = create_stage(desc.pixel_shader, "pixel", candidate.pixel_shader);
        }
        if (status)
        {
            status = create_stage(desc.compute_shader, "compute", candidate.compute_shader);
        }
        if (!status)
        {
            return RHIResult<RHIShaderProgramRef>::failure(status.code(), status.message());
        }

        RHIShaderProgramRef published = std::make_shared<const RHIShaderProgram>(std::move(candidate));
        programs_.emplace(std::move(key), published);
        return RHIResult<RHIShaderProgramRef>::success(std::move(published));
    }

    void RHIShaderProgramCache::clear()
    {
        programs_.clear();
    }

    std::size_t RHIShaderProgramCache::size() const
    {
        return programs_.size();
    }
} // namespace toy3d
