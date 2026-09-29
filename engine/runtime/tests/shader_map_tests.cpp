#include "rendercore/shader/shader_map.h"

#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/shader/rhi_shader_program.h"
#include "shader_map_test_utils.h"

#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    toy3d::ShaderContentHash nonzero_hash(std::uint8_t value)
    {
        toy3d::ShaderContentHash hash{};
        hash[0] = value;
        return hash;
    }

    toy3d::ShaderMapProgramData make_program()
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/Test/ShaderMap";
        program.pass_name = "Main";
        program.mapping_version = 1;
        program.logical_layout_hash = nonzero_hash(1);
        program.target_binding_hash = nonzero_hash(2);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = nonzero_hash(4);

        toy3d::ShaderMapBinding constants;
        constants.parameter_id = 10;
        constants.name = "ToyMaterialConstants";
        constants.group = toy3d::RHIBindingGroup::Material;
        constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        constants.stages = toy3d::RHIShaderStageFlags::Vertex;
        constants.target_binding = 0;
        constants.constant_buffer_size = 32;
        constants.constant_members.push_back({11, "base_color", toy3d::ShaderValueType::Float32x4, 0, 16, 0, 0});
        program.bindings.push_back(constants);

        toy3d::ShaderMapBinding texture;
        texture.parameter_id = 12;
        texture.name = "source_texture";
        texture.group = toy3d::RHIBindingGroup::Material;
        texture.type = toy3d::RHIResourceBindingType::SampledTexture;
        texture.stages = toy3d::RHIShaderStageFlags::Vertex;
        texture.target_binding = 1;
        program.bindings.push_back(texture);

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {1, 2, 3, 4};
        vertex.content_hash = nonzero_hash(5);
        vertex.reflection = program.bindings;
        vertex.interface_variables.push_back({"in.var.POSITION0", "POSITION0", 3u, true,
                                              toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u});
        program.stages.push_back(std::move(vertex));
        program.vertex_inputs.push_back({toy3d::ShaderVertexAttributeId::Position0, "POSITION", 0u,
                                         toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u, 3u});
        toy3d::tests::finalize_test_program_parameter_schema(program);
        return program;
    }

    class CountingLoader final : public toy3d::ShaderMapLoader
    {
      public:
        explicit CountingLoader(toy3d::ShaderMapProgramData program) : program_(std::move(program)) {}

        toy3d::ShaderMapProgramLoadResult load_program(const toy3d::ShaderMapProgramKey&) const override
        {
            ++load_count;
            return {program_, {}};
        }

        mutable std::uint32_t load_count = 0;

      private:
        toy3d::ShaderMapProgramData program_;
    };

    void test_shader_map_caches_full_identity_and_indexes_parameters()
    {
        toy3d::ShaderMapProgramData program = make_program();
        CountingLoader loader(program);
        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramKey key;
        key.shader_name = program.shader_name;
        key.pass_name = program.pass_name;
        key.permutation_key = program.permutation_key;

        toy3d::ShaderMapProgramResult first = shader_map.find_or_load(key);
        check(first.succeeded(), first.error.c_str());
        toy3d::ShaderMapProgramResult second = shader_map.find_or_load(key);
        check(second.succeeded(), second.error.c_str());
        check(first.program == second.program, "ShaderMap must return the cached immutable Program");
        check(loader.load_count == 1, "full identity cache hit must not call the Loader again");
        auto candidate = toy3d::ShaderMap::create_candidate(program, key);
        check(candidate.succeeded() && candidate.program != first.program,
              "a candidate with the same key must be a separate immutable Program");
        check(shader_map.find_or_load(key).program == first.program && loader.load_count == 1,
              "candidate creation must not replace an already published cache entry");
        auto invalid_candidate = program;
        invalid_candidate.shader_name = "Wrong/Shader";
        check(!toy3d::ShaderMap::create_candidate(std::move(invalid_candidate), key).succeeded(),
              "candidate creation must validate identity before publishing a Program");

        const toy3d::ShaderParameterBinding* constant_binding = first.program->find_parameter_binding(11);
        const auto* constant = constant_binding ? std::get_if<toy3d::ShaderConstantBinding>(constant_binding) : nullptr;
        check(constant, "constant member must be queryable by parameter ID");
        check(constant->group == toy3d::RHIBindingGroup::Material && constant->constant_buffer_binding == 0 &&
                  constant->offset == 0 && constant->size == 16 && constant->constant_buffer_size == 32,
              "constant lookup must preserve group, buffer binding, and byte range");

        const toy3d::ShaderParameterBinding* texture_binding = first.program->find_parameter_binding(12);
        const auto* texture = texture_binding ? std::get_if<toy3d::ShaderResourceBinding>(texture_binding) : nullptr;
        check(texture, "resource must be queryable by parameter ID");
        check(texture->group == toy3d::RHIBindingGroup::Material &&
                  texture->resource_type == toy3d::RHIResourceBindingType::SampledTexture &&
                  texture->target_binding == 1,
              "resource lookup must preserve group, type, and target binding");
        check(first.program->find_parameter_binding(999) == nullptr, "unknown parameter ID must return nullptr");

        auto rhi_program = toy3d::build_rhi_shader_program_desc(*first.program);
        check(rhi_program.succeeded(), rhi_program.status().message().c_str());
        check(rhi_program.value().vertex_shader.has_value() &&
                  rhi_program.value().vertex_shader->vertex_inputs.size() == 1u,
              "RHI Shader conversion must preserve vertex-input reflection");
        const toy3d::RHIShaderVertexInputReflection& rhi_input = rhi_program.value().vertex_shader->vertex_inputs[0];
        check(rhi_input.semantic_name == "POSITION" && rhi_input.semantic_index == 0u && rhi_input.location == 3u &&
                  rhi_input.scalar_type == toy3d::RHIShaderVertexInputReflection::ScalarType::Float32 &&
                  rhi_input.component_count == 4u,
              "RHI Shader conversion must preserve semantic, location, and data shape");
    }

    void test_invalid_metadata_and_identity_fail()
    {
        toy3d::ShaderMapProgramData program = make_program();
        toy3d::ShaderMapProgramData invalid = program;
        invalid.bindings[0].constant_members[0].size = 64;
        invalid.stages[0].reflection = invalid.bindings;
        toy3d::ShaderMapProgramKey key;
        key.shader_name = invalid.shader_name;
        key.pass_name = invalid.pass_name;
        key.permutation_key = invalid.permutation_key;
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
              "constant member outside its buffer must fail validation");

        toy3d::ShaderMapProgramData different_layout = program;
        toy3d::shader::ShaderParameterConstantBufferSchema& schema_buffer =
            different_layout.parameter_schema.constant_buffers[0];
        schema_buffer.members[0].offset = 16u;
        schema_buffer.data_layout_hash = toy3d::shader::calculate_constant_buffer_data_layout_hash(
            schema_buffer.group, schema_buffer.binding_id, schema_buffer.size,
            {{schema_buffer.members[0].parameter_id, schema_buffer.members[0].name,
              schema_buffer.members[0].type, schema_buffer.members[0].offset,
              schema_buffer.members[0].size, schema_buffer.members[0].array_stride,
              schema_buffer.members[0].matrix_stride}});
        different_layout.parameter_schema.logical_layout_hash =
            toy3d::shader::calculate_shader_parameter_logical_layout_hash(
                different_layout.parameter_schema);
        different_layout.parameter_schema.schema_identity =
            toy3d::shader::calculate_shader_parameter_schema_identity(
                different_layout.parameter_schema);
        different_layout.logical_layout_hash = different_layout.parameter_schema.logical_layout_hash;
        check(!toy3d::validate_shader_map_program(std::move(different_layout), key).succeeded(),
              "equal-size active constants with a different schema layout identity must fail publication");

        CountingLoader loader(std::move(program));
        toy3d::ShaderMap shader_map(loader);
        key.platform = toy3d::ShaderPlatform::D3D11SM5;
        check(!shader_map.find_or_load(key).succeeded(), "ShaderPlatform mismatch must fail through ShaderMap");
    }

    void test_local_vertex_factory_matches_fixed_shader_inputs()
    {
        toy3d::RHIBufferDesc position_desc;
        position_desc.size = 48u;
        position_desc.usage = toy3d::RHIResourceUsage::VertexBuffer;
        toy3d::RHIBufferRef position_buffer = std::make_shared<toy3d::RHIBuffer>(position_desc);

        toy3d::RHIBufferDesc static_desc;
        static_desc.size = 72u;
        static_desc.usage = toy3d::RHIResourceUsage::VertexBuffer;
        toy3d::RHIBufferRef static_buffer = std::make_shared<toy3d::RHIBuffer>(static_desc);

        std::vector<toy3d::VertexStreamComponent> components = {
            {toy3d::ShaderVertexAttributeId::Position0, 0u, 0u, 16u, toy3d::PixelFormat::R32G32B32A32Float,
             position_buffer},
            {toy3d::ShaderVertexAttributeId::Normal0, 1u, 0u, 24u, toy3d::PixelFormat::R32G32B32A32Float,
             static_buffer},
            {toy3d::ShaderVertexAttributeId::TexCoord0, 1u, 16u, 24u, toy3d::PixelFormat::R32G32Float, static_buffer}};
        toy3d::LocalVertexFactory vertex_factory(std::move(components));

        const auto float_type = toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32;
        std::vector<toy3d::ShaderVertexInput> shader_inputs = {
            {toy3d::ShaderVertexAttributeId::Position0, "POSITION", 0u, float_type, 4u, 2u},
            {toy3d::ShaderVertexAttributeId::Normal0, "NORMAL", 0u, float_type, 4u, 0u},
            {toy3d::ShaderVertexAttributeId::TexCoord0, "TEXCOORD", 0u, float_type, 2u, 1u}};
        std::vector<toy3d::RHIGraphicsPipelineDesc::VertexBufferLayout> layouts;
        std::vector<toy3d::RHIGraphicsPipelineDesc::VertexAttribute> attributes;
        std::vector<toy3d::RHIVertexBufferBinding> bindings;
        const toy3d::RHIStatus status = vertex_factory.build_vertex_input(shader_inputs, layouts, attributes, bindings);
        check(static_cast<bool>(status), status.message().c_str());
        check(layouts.size() == 2u && bindings.size() == 2u && attributes.size() == 3u && layouts[0].binding == 0u &&
                  layouts[1].binding == 1u && attributes[0].location == 0u && attributes[1].location == 1u &&
                  attributes[2].location == 2u,
              "LocalVertexFactory must emit sorted pipeline layouts and draw bindings");

        shader_inputs.push_back({toy3d::ShaderVertexAttributeId::Color0, "COLOR", 0u, float_type, 4u, 3u});
        check(!vertex_factory.build_vertex_input(shader_inputs, layouts, attributes, bindings) && layouts.empty() &&
                  attributes.empty() && bindings.empty(),
              "a Shader requiring absent optional COLOR0 must fail without partial output");
    }

} // namespace

int main()
{
    try
    {
        test_shader_map_caches_full_identity_and_indexes_parameters();
        test_invalid_metadata_and_identity_fail();
        test_local_vertex_factory_matches_fixed_shader_inputs();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
