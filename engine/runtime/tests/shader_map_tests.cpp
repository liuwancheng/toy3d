#include "rendercore/shader/shader_map.h"

#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
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
        program.pass_template_hash = nonzero_hash(3);
        program.permutation_key = nonzero_hash(4);

        toy3d::ShaderMapBinding constants;
        constants.parameter_id = 10;
        constants.name = "ToyMaterialConstants";
        constants.group = toy3d::RHIBindingGroup::Material;
        constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        constants.stages = toy3d::RHIShaderStageFlags::Vertex;
        constants.target_binding = 0;
        constants.constant_buffer_size = 32;
        constants.constant_members.push_back({11, "base_color",
            toy3d::ShaderValueType::Float32x4, 0, 16, 0, 0});
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
        program.stages.push_back(std::move(vertex));
        return program;
    }

    class CountingLoader final : public toy3d::ShaderMapLoader
    {
    public:
        explicit CountingLoader(toy3d::ShaderMapProgramData program)
            : program_(std::move(program))
        {
        }

        toy3d::ShaderMapProgramLoadResult load_program(
            const toy3d::ShaderMapProgramKey&) const override
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

        const toy3d::ShaderParameterBinding* constant_binding =
            first.program->find_parameter_binding(11);
        const auto* constant = constant_binding ?
            std::get_if<toy3d::ShaderConstantBinding>(constant_binding) : nullptr;
        check(constant,
            "constant member must be queryable by parameter ID");
        check(constant->group == toy3d::RHIBindingGroup::Material &&
              constant->constant_buffer_binding == 0 && constant->offset == 0 &&
              constant->size == 16 && constant->constant_buffer_size == 32,
            "constant lookup must preserve group, buffer binding, and byte range");

        const toy3d::ShaderParameterBinding* texture_binding =
            first.program->find_parameter_binding(12);
        const auto* texture = texture_binding ?
            std::get_if<toy3d::ShaderResourceBinding>(texture_binding) : nullptr;
        check(texture,
            "resource must be queryable by parameter ID");
        check(texture->group == toy3d::RHIBindingGroup::Material &&
              texture->resource_type == toy3d::RHIResourceBindingType::SampledTexture &&
              texture->target_binding == 1,
            "resource lookup must preserve group, type, and target binding");
        check(first.program->find_parameter_binding(999) == nullptr,
            "unknown parameter ID must return nullptr");
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

        CountingLoader loader(std::move(program));
        toy3d::ShaderMap shader_map(loader);
        key.platform = toy3d::ShaderPlatform::D3D11SM5;
        check(!shader_map.find_or_load(key).succeeded(),
            "ShaderPlatform mismatch must fail through ShaderMap");
    }

}

int main()
{
    try
    {
        test_shader_map_caches_full_identity_and_indexes_parameters();
        test_invalid_metadata_and_identity_fail();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
