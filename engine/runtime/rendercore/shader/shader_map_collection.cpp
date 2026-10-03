#include "rendercore/shader/shader_map_collection.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    // --------------------------------------------------------------------------
    // ShaderMapCollectionResult: validated immutable collection publication
    // --------------------------------------------------------------------------
    bool ShaderMapCollectionResult::succeeded() const
    {
        return collection != nullptr && error.empty();
    }

    // --------------------------------------------------------------------------
    // ShaderMapCollection: source/configuration program queries without I/O
    // --------------------------------------------------------------------------
    ShaderMapCollection::ShaderMapCollection(shader::ShaderMapIndex index, std::vector<ShaderMapProgramRef> programs)
        : index_(std::move(index)), programs_(std::move(programs))
    {
        shader::ShaderCompileSource source;
        source.usage = index_.programs.front().contract.usage;
        source.material_domain = index_.material_domain;
        source.features = index_.features;
        std::string error;
        // Index admission has already evaluated this immutable configuration.
        shader::resolve_shader_engine_features(source, index_.material_selections, index_.policy, features_, error);
        bool required = false;
        shader::evaluate_shader_static_condition(index_.tangent_frame_when, index_.material_domain,
                                                 index_.material_selections, index_.policy, required, error);
        requires_tangent_frame_ = index_.declares_tangent_frame && required;
    }

    const shader::ShaderMapIndex& ShaderMapCollection::index() const
    {
        return index_;
    }

    const std::vector<ShaderMapProgramRef>& ShaderMapCollection::programs() const
    {
        return programs_;
    }

    bool ShaderMapCollection::requires_tangent_frame() const
    {
        return requires_tangent_frame_;
    }

    const shader::ShaderEngineFeatures& ShaderMapCollection::features() const
    {
        return features_;
    }

    ShaderMapCollectionResult ShaderMapCollection::create_candidate(ShaderMapCollectionLoadResult loaded)
    {
        ShaderMapCollectionResult result;
        if (!loaded.succeeded())
        {
            result.error = loaded.error.empty() ? "ShaderMap collection data is empty." : std::move(loaded.error);
            return result;
        }
        if (!shader::validate_shader_map_index(loaded.index, result.error) ||
            loaded.programs.size() != loaded.index.programs.size())
        {
            if (result.error.empty())
            {
                result.error = "ShaderMap collection is incomplete.";
            }
            return result;
        }
        std::vector<ShaderMapProgramRef> programs;
        for (std::size_t number = 0u; number < loaded.programs.size(); ++number)
        {
            auto& data = loaded.programs[number];
            const auto& record = loaded.index.programs[number];
            if (data.contract.usage != record.contract.usage || data.contract.geometry != record.contract.geometry ||
                data.contract.surface_mode != record.contract.surface_mode ||
                data.contract.vertex_factory_support != record.contract.vertex_factory_support)
            {
                result.error = "ShaderMap collection programs must match their index and have no attached programs.";
                return result;
            }
            ShaderMapProgramKey key;
            key.shader_name = loaded.index.shader_name;
            key.pass_name = record.pass_name;
            key.permutation_key = loaded.index.permutation_key;
            key.role = record.contract.role;
            key.vertex_factory = record.contract.vertex_factory;
            key.pass_permutation_key = record.pass_permutation_key;
            if (key.vertex_factory == shader::VertexFactoryType::GPUSkin)
            {
                const auto bone_id = shader::make_shader_parameter_id(
                    shader::BindingGroup::Object, shader::ShaderParameterCategory::ReadOnlyBuffer, "toy_bone_matrices");
                if (std::none_of(data.bindings.begin(), data.bindings.end(),
                                 [bone_id](const ShaderMapBinding& binding)
                                 {
                                     return binding.parameter_id == bone_id &&
                                            binding.type == RHIResourceBindingType::ReadOnlyTypedBuffer &&
                                            binding.group == RHIBindingGroup::Object &&
                                            binding.stages == RHIShaderStageFlags::Vertex;
                                 }))
                {
                    result.error = "GPUSkin program must read the engine bone typed buffer in its vertex stage.";
                    return result;
                }
                for (const auto attribute :
                     {ShaderVertexAttributeId::BlendIndices0, ShaderVertexAttributeId::BlendWeights0,
                      ShaderVertexAttributeId::BlendIndices1, ShaderVertexAttributeId::BlendWeights1})
                {
                    if (std::none_of(data.vertex_inputs.begin(), data.vertex_inputs.end(),
                                     [attribute](const ShaderVertexInput& input)
                                     {
                                         return input.attribute_id == attribute;
                                     }))
                    {
                        result.error = "GPUSkin program must consume both skin influence groups.";
                        return result;
                    }
                }
            }
            auto program = ShaderMap::create_candidate(std::move(data), key);
            if (!program.succeeded())
            {
                result.error = std::move(program.error);
                return result;
            }
            if (!programs.empty() && record.contract.usage == shader::ShaderUsage::Material &&
                shader::calculate_shader_parameter_group_identity(program.program->data().parameter_schema,
                                                                  shader::BindingGroup::Material) !=
                    shader::calculate_shader_parameter_group_identity(programs.front()->data().parameter_schema,
                                                                      shader::BindingGroup::Material))
            {
                result.error =
                    "Material programs must preserve one full Material parameter schema across roles/factories.";
                return result;
            }
            programs.push_back(std::move(program.program));
        }
        for (const auto& program : programs)
        {
            const auto& data = program->data();
            for (const auto& other : programs)
            {
                const auto& peer = other->data();
                if (data.pass_name == peer.pass_name && (data.pass_template_hash != peer.pass_template_hash ||
                                                         shader::calculate_shader_parameter_group_identity(
                                                             data.parameter_schema, shader::BindingGroup::Pass) !=
                                                             shader::calculate_shader_parameter_group_identity(
                                                                 peer.parameter_schema, shader::BindingGroup::Pass)))
                {
                    result.error = "Factory programs for the same Pass must preserve its schema and graphics state.";
                    return result;
                }
            }
        }
        ShaderMapCollection collection(std::move(loaded.index), std::move(programs));
        result.collection = std::make_shared<ShaderMapCollection>(std::move(collection));
        return result;
    }

    ShaderMapProgramResult ShaderMapCollection::find(
        shader::ShaderPassRole role, shader::VertexFactoryType factory, const std::string& global_pass_name,
        const std::vector<shader::ShaderPermutationSelection>& pass_selections) const
    {
        if ((role == shader::ShaderPassRole::Global) != !global_pass_name.empty())
        {
            return {nullptr, "Global queries require an explicit Pass name; mesh queries use role/VertexFactory."};
        }
        const auto configuration =
            shader::resolve_shader_permutation(shader::shader_pass_domain(role, features_), pass_selections);
        if (!configuration.succeeded())
        {
            return {nullptr, configuration.errors.front().message};
        }
        for (const auto& program : programs_)
        {
            const auto& data = program->data();
            if (data.contract.role == role && data.contract.vertex_factory == factory &&
                data.pass_permutation_key == configuration.permutation->key &&
                (global_pass_name.empty() || data.pass_name == global_pass_name))
            {
                return {program, {}};
            }
        }
        return {nullptr, "ShaderMap collection is missing the requested role/VertexFactory/Pass program."};
    }
} // namespace toy3d
