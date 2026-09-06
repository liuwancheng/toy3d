#include "rendercore/shader/loaders/shader_map_entry_loader.h"

#include "rendercore/shader/rhi_shader_program.h"
#include "rendercore/shader/shader_map.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    const toy3d::ShaderMapBinding* find_binding(
        const toy3d::ShaderMapProgramData& program,
        const std::string& name)
    {
        const auto found = std::find_if(program.bindings.begin(), program.bindings.end(),
            [&](const toy3d::ShaderMapBinding& binding) { return binding.name == name; });
        return found == program.bindings.end() ? nullptr : &*found;
    }

    const toy3d::ShaderMapStage* find_stage(
        const toy3d::ShaderMapProgramData& program,
        toy3d::RHIShaderStage stage)
    {
        const auto found = std::find_if(
            program.stages.begin(), program.stages.end(),
            [&](const toy3d::ShaderMapStage& candidate) {
                return candidate.stage == stage;
            });
        return found == program.stages.end() ? nullptr : &*found;
    }

    void test_verified_entry_loads()
    {
        toy3d::ShaderMapEntryLoader loader(
            toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_TEST_ROOT));
        toy3d::ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Test/TestPass";
        key.pass_name = "TestPass";
        toy3d::ShaderMapProgramLoadResult loaded = loader.load_program(key);
        check(loaded.succeeded(), loaded.error.c_str());
        check(loaded.program->stages.size() == 2, "test Program must contain vertex and pixel stages");
        check(loaded.program->vertex_inputs.empty(),
            "SV_VertexID-only test Shader must not invent a logical vertex input");
        check(loaded.program->graphics_pass_state.depth_test_enable &&
              loaded.program->graphics_pass_state.depth_compare_operation ==
                  toy3d::shader::ShaderGraphicsPassState::CompareOperation::GreaterEqual &&
              toy3d::shader::calculate_shader_graphics_pass_state_hash(
                  loaded.program->graphics_pass_state) ==
                  loaded.program->pass_template_hash,
            "loader must retain the verified normalized graphics Pass state");

        const toy3d::ShaderMapStage* vertex_stage = find_stage(
            *loaded.program, toy3d::RHIShaderStage::Vertex);
        const toy3d::ShaderMapStage* pixel_stage = find_stage(
            *loaded.program, toy3d::RHIShaderStage::Pixel);
        check(vertex_stage && pixel_stage,
            "test Program must retain both graphics stages");
        check(vertex_stage->interface_variables.size() == 1 &&
              !vertex_stage->interface_variables[0].input &&
              !vertex_stage->interface_variables[0].name.empty() &&
              vertex_stage->interface_variables[0].location == 0u &&
              vertex_stage->interface_variables[0].scalar_type ==
                  toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32 &&
              vertex_stage->interface_variables[0].component_count == 2u,
            "loader must preserve complete vertex-stage interface metadata");
        check(pixel_stage->interface_variables.size() == 2 &&
              pixel_stage->interface_variables[0].input &&
              pixel_stage->interface_variables[0].location == 0u &&
              pixel_stage->interface_variables[0].component_count == 2u,
            "loader must preserve complete pixel-stage interface metadata");

        const toy3d::ShaderMapBinding* texture = find_binding(*loaded.program, "source_texture");
        const toy3d::ShaderMapBinding* sampler = find_binding(*loaded.program, "source_sampler");
        const toy3d::ShaderMapBinding* global_texture = find_binding(
            *loaded.program, "global_texture");
        const toy3d::ShaderMapBinding* view_texture = find_binding(
            *loaded.program, "view_texture");
        check(texture && sampler, "test Program must expose texture and sampler bindings");
        check(texture->group == toy3d::RHIBindingGroup::Material && texture->target_binding == 0,
            "texture must use Vulkan ES3.1 Material binding 0");
        check(sampler->group == toy3d::RHIBindingGroup::Material && sampler->target_binding == 1,
            "sampler must use Vulkan ES3.1 Material binding 1");
        check(global_texture && view_texture &&
              global_texture->group == toy3d::RHIBindingGroup::Global &&
              view_texture->group == toy3d::RHIBindingGroup::View &&
              global_texture->target_binding == 0 && view_texture->target_binding == 1,
            "Global and View must use compact Vulkan ES3.1 set 0 bindings");

        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramResult mapped = shader_map.find_or_load(key);
        check(mapped.succeeded(), mapped.error.c_str());
        auto rhi_desc = toy3d::build_rhi_shader_program_desc(*mapped.program);
        check(rhi_desc.succeeded(), rhi_desc.status().message().c_str());
        check(rhi_desc.value().vertex_shader.has_value() &&
              rhi_desc.value().pixel_shader.has_value() &&
              !rhi_desc.value().compute_shader.has_value(),
            "RHI Program conversion must preserve the graphics stage set");
        check(rhi_desc.value().binding_layout.entries.size() == 4,
            "RHI binding layout must be generated from ShaderMap reflection");
        check(rhi_desc.value().pixel_shader->reflection.size() == 4,
            "pixel Shader reflection must be generated from the verified entry");

        toy3d::ShaderMapProgramData invalid = *loaded.program;
        invalid.stages.back().reflection.clear();
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "missing required stage reflection must fail runtime validation");

        invalid = *loaded.program;
        invalid.graphics_pass_state.cull_mode =
            toy3d::shader::ShaderGraphicsPassState::CullMode::Front;
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "runtime validation must reject graphics state that does not match its template hash");

        invalid = *loaded.program;
        auto invalid_global = std::find_if(invalid.bindings.begin(), invalid.bindings.end(),
            [](const toy3d::ShaderMapBinding& binding) {
                return binding.group == toy3d::RHIBindingGroup::Global;
            });
        auto invalid_view = std::find_if(invalid.bindings.begin(), invalid.bindings.end(),
            [](const toy3d::ShaderMapBinding& binding) {
                return binding.group == toy3d::RHIBindingGroup::View;
            });
        check(invalid_global != invalid.bindings.end() &&
              invalid_view != invalid.bindings.end(),
            "test Program must retain Global and View bindings for corruption tests");
        invalid_view->target_binding = invalid_global->target_binding;
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "duplicate Vulkan set/binding must fail runtime validation");

        invalid = *loaded.program;
        toy3d::ShaderMapStage* invalid_vertex = nullptr;
        for (toy3d::ShaderMapStage& stage : invalid.stages)
        {
            if (stage.stage == toy3d::RHIShaderStage::Vertex)
            {
                invalid_vertex = &stage;
                break;
            }
        }
        check(invalid_vertex != nullptr,
            "test Program must retain a vertex stage for interface corruption tests");
        invalid_vertex->interface_variables.push_back(
            {"position", "POSITION0", 0u, true,
             toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u});
        invalid_vertex->interface_variables.push_back(
            {"position_duplicate", "position0", 1u, true,
             toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u});
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "duplicate logical vertex attributes must fail runtime validation");

        invalid = *loaded.program;
        for (toy3d::ShaderMapStage& stage : invalid.stages)
        {
            if (stage.stage == toy3d::RHIShaderStage::Vertex)
            {
                stage.interface_variables.push_back(
                    {"position", "POSITION0", 1u, true,
                     toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 2u});
                break;
            }
        }
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "unsupported vertex input shape must fail runtime validation");

        invalid = *loaded.program;
        for (toy3d::ShaderMapStage& stage : invalid.stages)
        {
            if (stage.stage == toy3d::RHIShaderStage::Vertex)
            {
                stage.interface_variables.push_back(
                     {"position", "POSITION0",
                     std::numeric_limits<std::uint32_t>::max(), true,
                     toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u});
                break;
            }
        }
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "missing vertex target mapping must fail runtime validation");

        invalid = *loaded.program;
        for (toy3d::ShaderMapStage& stage : invalid.stages)
        {
            if (stage.stage == toy3d::RHIShaderStage::Pixel)
            {
                stage.interface_variables[0].component_count = 3u;
                break;
            }
        }
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "conflicting graphics Program interfaces must fail runtime validation");
    }

    void test_target_and_identity_mismatch_fail()
    {
        toy3d::ShaderMapEntryLoader loader(
            toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_TEST_ROOT));
        toy3d::ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Test/Missing";
        key.pass_name = "TestPass";
        check(!loader.load_program(key).succeeded(),
            "unknown Shader identity must fail diagnostically");

        key.shader_name = "Toy3d/Test/TestPass";
        key.platform = toy3d::ShaderPlatform::D3D11SM5;
        check(!loader.load_program(key).succeeded(),
            "unsupported ShaderPlatform must fail diagnostically");
    }

    void test_shader_vertex_input_conversion_and_parity()
    {
        toy3d::shader::ReflectedInterfaceVariable reflected;
        reflected.name = "in.var.POSITION0";
        reflected.semantic = "position0";
        reflected.location = 3u;
        reflected.input = true;
        reflected.scalar_type =
            toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32;
        reflected.component_count = 4u;

        toy3d::ShaderVertexInput vulkan_input;
        std::string error;
        check(toy3d::try_make_shader_vertex_input(
                reflected, vulkan_input, error),
            error.c_str());
        check(vulkan_input.attribute_id ==
                  toy3d::ShaderVertexAttributeId::Position0 &&
              vulkan_input.semantic_name == "POSITION" &&
              vulkan_input.semantic_index == 0u &&
              vulkan_input.component_count == 4u &&
              vulkan_input.target_location == 3u,
            "POSITION0 reflection must convert to the canonical runtime contract");

        toy3d::ShaderVertexInput other_target_input;
        reflected.semantic.clear();
        check(toy3d::try_make_shader_vertex_input(
                reflected, other_target_input, error) &&
              other_target_input.attribute_id ==
                  toy3d::ShaderVertexAttributeId::Position0,
            "SPIR-V interface names must provide a fallback logical vertex semantic");

        reflected.semantic = "POSITION0";
        reflected.component_count = 2u;
        check(toy3d::try_make_shader_vertex_input(
                reflected, other_target_input, error) &&
              other_target_input.attribute_id ==
                  toy3d::ShaderVertexAttributeId::Position0 &&
              other_target_input.component_count == 2u,
            "UI position reflection must support a float2 POSITION0 contract");

        reflected.semantic = "NORMAL";
        reflected.component_count = 4u;
        check(toy3d::try_make_shader_vertex_input(
                reflected, other_target_input, error) &&
              other_target_input.attribute_id ==
                  toy3d::ShaderVertexAttributeId::Normal0,
            "NORMAL without an explicit zero index must normalize to NORMAL0");
        reflected.semantic = "TEXCOORD0";
        reflected.component_count = 2u;
        check(toy3d::try_make_shader_vertex_input(
                reflected, other_target_input, error) &&
              other_target_input.attribute_id ==
                  toy3d::ShaderVertexAttributeId::TexCoord0,
            "TEXCOORD0 must convert to the fixed logical attribute set");
        reflected.semantic = "COLOR0";
        reflected.component_count = 4u;
        check(toy3d::try_make_shader_vertex_input(
                reflected, other_target_input, error) &&
              other_target_input.attribute_id ==
                  toy3d::ShaderVertexAttributeId::Color0,
            "optional COLOR0 must convert to the fixed logical attribute set");

        other_target_input = vulkan_input;
        other_target_input.target_location = 0u;
        check(toy3d::have_same_shader_vertex_input_contract(
                vulkan_input, other_target_input),
            "cross-target parity must ignore native target location");
        other_target_input.component_count = 3u;
        check(!toy3d::have_same_shader_vertex_input_contract(
                vulkan_input, other_target_input),
            "cross-target parity must compare logical data shape");

        reflected.semantic = "TANGENT0";
        check(!toy3d::try_make_shader_vertex_input(
                reflected, other_target_input, error),
            "the first-stage contract must reject unsupported logical attributes");
    }
}

int main()
{
    try
    {
        test_verified_entry_loads();
        test_target_and_identity_mismatch_fail();
        test_shader_vertex_input_conversion_and_parity();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
