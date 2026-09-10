#include "shader_parameters/builtin_shader_parameters.generated.h"
#include "shader_parameters/toy3d_postprocess_tonemap.generated.h"
#include "shader_parameters/toy3d_ui_imgui.generated.h"

#include <iostream>
#include <type_traits>
#include <utility>

namespace toy3d
{
    namespace shader_parameters_compile_fixture
    {
        struct ShadowStyleParameters
        {
            float depth_bias{};
        };

        inline const ShaderParametersMetadata& shader_parameters_metadata(const ShadowStyleParameters&)
        {
            static const ShaderParametersMetadata metadata = {
                shader::BindingGroup::Pass,
                1u,
                1u,
                1u,
                1u,
                {},
                {},
                {1u,
                 16u,
                 {},
                 1u,
                 {{2u, shader::ShaderValueType::Float32, 0u, 4u, 1u, 0u, 0u, {}}}},
                {}};
            return metadata;
        }

        inline void encode_shader_parameters(const ShadowStyleParameters& parameters,
                                             ShaderParameterEncoder& encoder)
        {
            const ShaderParametersMetadata& metadata = shader_parameters_metadata(parameters);
            encoder.write_constant(metadata.constant_buffer.members[0u], parameters.depth_bias);
        }
    } // namespace shader_parameters_compile_fixture
} // namespace toy3d

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    void compile_typed_binding_calls(toy3d::RHIDevice& device, toy3d::RHICommandContext& context)
    {
        const toy3d::TonemapPassParameters tonemap;
        const toy3d::shader_parameters_compile_fixture::ShadowStyleParameters shadow;
        const auto tonemap_result = toy3d::create_transient_shader_binding(device, context, tonemap);
        const auto shadow_result = toy3d::create_transient_shader_binding(device, context, shadow);
        (void)tonemap_result;
        (void)shadow_result;
    }
} // namespace

int main()
{
    (void)&compile_typed_binding_calls;
    static_assert(std::is_same<decltype(toy3d::TonemapPassParameters::exposure_ev), float>::value,
                  "Tonemap exposure must remain a typed float field.");
    static_assert(std::is_same<decltype(toy3d::ImGuiPassParameters::projection), toy3d::Matrix4>::value,
                  "ImGui projection must remain a typed Matrix4 field.");
    static_assert(std::is_same<decltype(toy3d::ViewShaderParameters::toy_camera_position), toy3d::Vector3>::value,
                  "View camera position must remain a typed Vector3 field.");
    static_assert(std::is_same<decltype(toy3d::ObjectShaderParameters::toy_object_to_world), toy3d::Matrix4>::value,
                  "Object transform must remain a typed Matrix4 field.");
    using TonemapBindingResult = decltype(toy3d::create_transient_shader_binding(
        std::declval<toy3d::RHIDevice&>(), std::declval<toy3d::RHICommandContext&>(),
        std::declval<const toy3d::TonemapPassParameters&>()));
    using ShadowBindingResult = decltype(toy3d::create_transient_shader_binding(
        std::declval<toy3d::RHIDevice&>(), std::declval<toy3d::RHICommandContext&>(),
        std::declval<const toy3d::shader_parameters_compile_fixture::ShadowStyleParameters&>()));
    static_assert(std::is_same<TonemapBindingResult, toy3d::RHIResult<toy3d::RHIBindingSetRef>>::value,
                  "Tonemap parameters must call the typed transient binding entry directly.");
    static_assert(std::is_same<ShadowBindingResult, toy3d::RHIResult<toy3d::RHIBindingSetRef>>::value,
                  "Shadow-style parameters must call the same entry without a binder or adapter.");

    const toy3d::TonemapPassParameters tonemap;
    const toy3d::ImGuiPassParameters imgui;
    const toy3d::ViewShaderParameters view;
    const toy3d::ObjectShaderParameters object;
    check(toy3d::shader_parameters_metadata(tonemap).group == toy3d::shader::BindingGroup::Pass,
          "Tonemap generated metadata must describe the Pass group.");
    check(toy3d::shader_parameters_metadata(imgui).group == toy3d::shader::BindingGroup::Pass,
          "ImGui generated metadata must describe the Pass group.");
    check(toy3d::shader_parameters_metadata(view).group == toy3d::shader::BindingGroup::View,
          "View generated metadata must describe the View group.");
    check(toy3d::shader_parameters_metadata(object).group == toy3d::shader::BindingGroup::Object,
          "Object generated metadata must describe the Object group.");

    toy3d::ShaderParameterEncoder tonemap_encoder(toy3d::shader_parameters_metadata(tonemap));
    toy3d::encode_shader_parameters(tonemap, tonemap_encoder);
    toy3d::ShaderParameterEncoder imgui_encoder(toy3d::shader_parameters_metadata(imgui));
    toy3d::encode_shader_parameters(imgui, imgui_encoder);
    toy3d::ShaderParameterEncoder view_encoder(toy3d::shader_parameters_metadata(view));
    toy3d::encode_shader_parameters(view, view_encoder);
    toy3d::ShaderParameterEncoder object_encoder(toy3d::shader_parameters_metadata(object));
    toy3d::encode_shader_parameters(object, object_encoder);
    check(tonemap_encoder.succeeded() && imgui_encoder.succeeded() && view_encoder.succeeded() &&
              object_encoder.succeeded(),
          "Generated encode overloads must route every fixture field through the canonical encoder.");
    return failure_count == 0 ? 0 : 1;
}
