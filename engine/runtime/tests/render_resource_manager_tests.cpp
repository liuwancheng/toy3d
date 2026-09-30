#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_queue.h"
#include "rendercore/material/material.h"
#include "rendercore/material/material_asset_builder.h"
#include "rendercore/rendering_thread.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_parameters.h"
#include "rendercore/texture/texture.h"
#include "shader_map_test_utils.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/material/material_shader_bindings.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/object_shader_bindings.h"
#include "renderscene/pass/mesh_draw_command.h"
#include "renderscene/render_scene.h"
#include "renderscene/render_resource.h"
#include "renderscene/render_resource_manager.h"
#include "renderscene/renderer_frame.h"
#include "renderscene/renderer.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/viewport_output_target.h"
#include "renderscene/texture/texture_resource.h"
#include "renderscene/view/forward_scene_renderer.h"
#include "renderscene/view/view_shader_bindings.h"
#include "task_graph/task_graph.h"
#include "threading/thread_manager.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    template <typename MemberDescription, typename MemberDescription::type Member> struct PrivateMemberAccess
    {
        friend typename MemberDescription::type get(MemberDescription) { return Member; }
    };

    struct SceneRendererViewInfosMember
    {
        using type = std::vector<toy3d::ViewInfo>& (toy3d::SceneRenderer::*)();
        friend type get(SceneRendererViewInfosMember);
    };
    template struct PrivateMemberAccess<SceneRendererViewInfosMember, &toy3d::SceneRenderer::view_infos>;

    std::vector<toy3d::ViewInfo>& view_infos(toy3d::ForwardSceneRenderer& renderer)
    {
        toy3d::SceneRenderer& base_renderer = renderer;
        return (base_renderer.*get(SceneRendererViewInfosMember{}))();
    }

    static_assert(std::is_default_constructible<toy3d::MeshDrawCommand>::value,
                  "MeshDrawCommand must support deterministic default initialization");
    static_assert(std::is_move_constructible<toy3d::MeshDrawCommand>::value,
                  "MeshDrawCommand must remain a movable frame-local value");
    static_assert(std::is_default_constructible<toy3d::MeshPassDrawList>::value,
                  "MeshPassDrawList must support deterministic default initialization");
    static_assert(std::is_move_constructible<toy3d::MeshPassDrawList>::value,
                  "MeshPassDrawList must remain a movable frame-local value");

    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::ShaderContentHash nonzero_hash(std::uint8_t value)
    {
        toy3d::ShaderContentHash hash{};
        hash[0] = value;
        return hash;
    }

    toy3d::shader::BindingGroup to_shader_group(toy3d::RHIBindingGroup group)
    {
        switch (group)
        {
        case toy3d::RHIBindingGroup::Global:
            return toy3d::shader::BindingGroup::Global;
        case toy3d::RHIBindingGroup::View:
            return toy3d::shader::BindingGroup::View;
        case toy3d::RHIBindingGroup::Pass:
            return toy3d::shader::BindingGroup::Pass;
        case toy3d::RHIBindingGroup::Material:
            return toy3d::shader::BindingGroup::Material;
        case toy3d::RHIBindingGroup::Object:
            return toy3d::shader::BindingGroup::Object;
        case toy3d::RHIBindingGroup::Max:
            break;
        }
        return toy3d::shader::BindingGroup::Material;
    }

    void finalize_uniform_binding(toy3d::ShaderMapBinding& binding)
    {
        const auto to_format_value_type = [](toy3d::ShaderValueType type)
        {
            switch (type)
            {
            case toy3d::ShaderValueType::Float32:
                return toy3d::shader::ShaderValueType::Float32;
            case toy3d::ShaderValueType::Float32x3:
                return toy3d::shader::ShaderValueType::Float32x3;
            case toy3d::ShaderValueType::Float32x4:
                return toy3d::shader::ShaderValueType::Float32x4;
            case toy3d::ShaderValueType::Float32x4x4:
                return toy3d::shader::ShaderValueType::Float32x4x4;
            default:
                return toy3d::shader::ShaderValueType::Float32;
            }
        };
        std::vector<toy3d::shader::ReflectedConstantMember> members;
        members.reserve(binding.constant_members.size());
        for (const toy3d::ShaderMapBinding::ConstantMember& member : binding.constant_members)
        {
            members.push_back({member.parameter_id, member.name, to_format_value_type(member.type), member.offset, member.size,
                               member.array_stride, member.matrix_stride});
        }
        binding.data_layout_hash = toy3d::shader::calculate_constant_buffer_data_layout_hash(
            to_shader_group(binding.group), binding.parameter_id, binding.constant_buffer_size,
            members);
        binding.shader_abi_version = toy3d::shader::toy_shader_abi_version;
    }

    toy3d::ShaderMapProgramData make_material_program(const std::string& pass_name, std::uint8_t hash_seed)
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/Test/Material";
        program.pass_name = pass_name;
        program.mapping_version = toy3d::shader::vulkan_binding_mapping_version;
        program.logical_layout_hash = nonzero_hash(hash_seed);
        program.target_binding_hash = nonzero_hash(hash_seed + 1u);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = nonzero_hash(hash_seed + 3u);

        toy3d::ShaderMapBinding constants;
        constants.parameter_id = 10u;
        constants.name = "MaterialConstants";
        constants.group = toy3d::RHIBindingGroup::Material;
        constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        constants.stages = toy3d::RHIShaderStageFlags::Vertex;
        constants.target_binding = 0u;
        constants.constant_buffer_size = 32u;
        constants.constant_members.push_back({11u, "roughness", toy3d::ShaderValueType::Float32, 0u, 4u, 0u, 0u});
        constants.constant_members.push_back({13u, "base_color", toy3d::ShaderValueType::Float32x4, 16u, 16u, 0u, 0u});
        finalize_uniform_binding(constants);
        program.bindings.push_back(constants);

        toy3d::ShaderMapBinding texture;
        texture.parameter_id = 12u;
        texture.name = "base_color_texture";
        texture.group = toy3d::RHIBindingGroup::Material;
        texture.type = toy3d::RHIResourceBindingType::SampledTexture;
        texture.stages = toy3d::RHIShaderStageFlags::Vertex;
        texture.target_binding = 1u;
        program.bindings.push_back(texture);

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {1u, 2u, 3u, hash_seed};
        vertex.content_hash = nonzero_hash(hash_seed + 4u);
        vertex.reflection = program.bindings;
        program.stages.push_back(std::move(vertex));
        toy3d::tests::finalize_test_program_parameter_schema(program);
        for (toy3d::shader::ShaderParameterConstantMemberSchema& member :
             program.parameter_schema.constant_buffers.front().members)
        {
            member.default_value.resize(member.size, 0u);
            if (member.parameter_id == 11u)
            {
                const float default_roughness = 0.25f;
                std::memcpy(member.default_value.data(), &default_roughness, sizeof(default_roughness));
            }
            else if (member.parameter_id == 13u)
            {
                const float default_base_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
                std::memcpy(member.default_value.data(), default_base_color, sizeof(default_base_color));
            }
        }
        program.parameter_schema.resources.front().default_value_kind =
            toy3d::shader::ShaderParameterDefaultValueKind::String;
        program.parameter_schema.resources.front().default_value = "Builtin/White";
        program.parameter_schema.logical_layout_hash =
            toy3d::shader::calculate_shader_parameter_logical_layout_hash(program.parameter_schema);
        program.parameter_schema.schema_identity =
            toy3d::shader::calculate_shader_parameter_schema_identity(program.parameter_schema);
        program.logical_layout_hash = program.parameter_schema.logical_layout_hash;
        return program;
    }

    toy3d::shader::ShaderParameterSchema material_schema_from_program(const toy3d::ShaderMapProgram& program)
    {
        return toy3d::material_parameter_schema_from_shader_schema(program.data().parameter_schema);
    }

    class MaterialProgramLoader final : public toy3d::ShaderMapLoader
    {
      public:
        explicit MaterialProgramLoader(toy3d::ShaderMapProgramData program) : program_(std::move(program)) {}

        toy3d::ShaderMapProgramLoadResult load_program(const toy3d::ShaderMapProgramKey&) const override
        {
            return {program_, {}};
        }

      private:
        toy3d::ShaderMapProgramData program_;
    };

    toy3d::ShaderMapProgramData make_view_object_program()
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/Test/ViewObject";
        program.pass_name = "Forward";
        program.mapping_version = toy3d::shader::vulkan_binding_mapping_version;
        program.logical_layout_hash = nonzero_hash(60u);
        program.target_binding_hash = nonzero_hash(61u);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = nonzero_hash(62u);

        toy3d::ShaderMapBinding view;
        view.parameter_id = toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View, toy3d::shader::ShaderParameterCategory::Constant, "");
        view.name = "toy_view_data";
        view.group = toy3d::RHIBindingGroup::View;
        view.type = toy3d::RHIResourceBindingType::UniformBuffer;
        view.stages = toy3d::RHIShaderStageFlags::Vertex;
        view.target_binding = 0u;
        view.constant_buffer_size = 416u;
        view.constant_members.push_back(
            {toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::View,
                                                     toy3d::shader::ShaderParameterCategory::Constant, "toy_view"),
             "toy_view", toy3d::ShaderValueType::Float32x4x4, 0u, 64u, 0u, 16u});
        view.constant_members.push_back(
            {toy3d::shader::make_shader_parameter_id(
                 toy3d::shader::BindingGroup::View, toy3d::shader::ShaderParameterCategory::Constant, "toy_projection"),
             "toy_projection", toy3d::ShaderValueType::Float32x4x4, 64u, 64u, 0u, 16u});
        view.constant_members.push_back(
            {toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::View,
                                                     toy3d::shader::ShaderParameterCategory::Constant,
                                                     "toy_view_projection"),
             "toy_view_projection", toy3d::ShaderValueType::Float32x4x4, 128u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
                                             toy3d::shader::BindingGroup::View,
                                             toy3d::shader::ShaderParameterCategory::Constant, "toy_inverse_view"),
                                         "toy_inverse_view", toy3d::ShaderValueType::Float32x4x4, 192u, 64u, 0u, 16u});
        view.constant_members.push_back(
            {toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::View,
                                                     toy3d::shader::ShaderParameterCategory::Constant,
                                                     "toy_inverse_projection"),
             "toy_inverse_projection", toy3d::ShaderValueType::Float32x4x4, 256u, 64u, 0u, 16u});
        view.constant_members.push_back(
            {toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::View,
                                                     toy3d::shader::ShaderParameterCategory::Constant,
                                                     "toy_inverse_view_projection"),
             "toy_inverse_view_projection", toy3d::ShaderValueType::Float32x4x4, 320u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
                                             toy3d::shader::BindingGroup::View,
                                             toy3d::shader::ShaderParameterCategory::Constant, "toy_camera_position"),
                                         "toy_camera_position", toy3d::ShaderValueType::Float32x3, 384u, 12u, 0u, 0u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
                                             toy3d::shader::BindingGroup::View,
                                             toy3d::shader::ShaderParameterCategory::Constant, "toy_camera_direction"),
                                         "toy_camera_direction", toy3d::ShaderValueType::Float32x3, 400u, 12u, 0u, 0u});
        finalize_uniform_binding(view);
        program.bindings.push_back(view);

        toy3d::ShaderMapBinding object;
        object.parameter_id = toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::Object, toy3d::shader::ShaderParameterCategory::Constant, "");
        object.name = "toy_object_data";
        object.group = toy3d::RHIBindingGroup::Object;
        object.type = toy3d::RHIResourceBindingType::UniformBuffer;
        object.stages = toy3d::RHIShaderStageFlags::Vertex;
        object.target_binding = 0u;
        object.constant_buffer_size = 64u;
        object.constant_members.push_back(
            {toy3d::shader::make_shader_parameter_id(toy3d::shader::BindingGroup::Object,
                                                     toy3d::shader::ShaderParameterCategory::Constant,
                                                     "toy_object_to_world"),
             "toy_object_to_world", toy3d::ShaderValueType::Float32x4x4, 0u, 64u, 0u, 16u});
        finalize_uniform_binding(object);
        program.bindings.push_back(object);

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {1u, 2u, 3u, 60u};
        vertex.content_hash = nonzero_hash(63u);
        vertex.reflection = program.bindings;
        program.stages.push_back(std::move(vertex));
        toy3d::tests::finalize_test_program_parameter_schema(program);
        return program;
    }

    toy3d::ShaderMapProgramData make_base_pass_program()
    {
        toy3d::ShaderMapProgramData program = make_view_object_program();
        program.vertex_inputs = {{toy3d::ShaderVertexAttributeId::Position0, "POSITION", 0u,
                                  toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u, 0u},
                                 {toy3d::ShaderVertexAttributeId::Normal0, "NORMAL", 0u,
                                  toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u, 1u},
                                 {toy3d::ShaderVertexAttributeId::TexCoord0, "TEXCOORD", 0u,
                                  toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 2u, 2u}};
        program.stages.front().interface_variables = {
            {"in.var.POSITION0", "POSITION0", 0u, true, toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32,
             4u},
            {"in.var.NORMAL0", "NORMAL0", 1u, true, toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u},
            {"in.var.TEXCOORD0", "TEXCOORD0", 2u, true, toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32,
             2u}};

        toy3d::ShaderMapStage pixel;
        pixel.stage = toy3d::RHIShaderStage::Pixel;
        pixel.entry_point = "ps_main";
        pixel.binary = {4u, 3u, 2u, 1u};
        pixel.content_hash = nonzero_hash(64u);
        program.stages.push_back(std::move(pixel));
        toy3d::tests::finalize_test_program_parameter_schema(program);
        return program;
    }

    toy3d::ShaderMapProgramData make_base_pass_program_with_required_material()
    {
        toy3d::ShaderMapProgramData program = make_base_pass_program();
        program.shader_name = "Toy3d/Test/ViewObjectMaterial";

        toy3d::ShaderMapBinding material_constants;
        material_constants.parameter_id = 10u;
        material_constants.name = "MaterialConstants";
        material_constants.group = toy3d::RHIBindingGroup::Material;
        material_constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        material_constants.stages = toy3d::RHIShaderStageFlags::Pixel;
        material_constants.target_binding = 0u;
        material_constants.constant_buffer_size = 16u;
        material_constants.constant_members.push_back(
            {11u, "required_value", toy3d::ShaderValueType::Float32, 0u, 4u, 0u, 0u});
        finalize_uniform_binding(material_constants);
        program.bindings.push_back(material_constants);
        program.stages.back().reflection.push_back(material_constants);
        toy3d::tests::finalize_test_program_parameter_schema(program);
        toy3d::shader::ShaderParameterConstantMemberSchema& member =
            program.parameter_schema.constant_buffers.back().members.front();
        member.default_value.resize(member.size, 0u);
        const float default_value = 0.25f;
        std::memcpy(member.default_value.data(), &default_value, sizeof(default_value));
        program.parameter_schema.logical_layout_hash =
            toy3d::shader::calculate_shader_parameter_logical_layout_hash(program.parameter_schema);
        program.parameter_schema.schema_identity =
            toy3d::shader::calculate_shader_parameter_schema_identity(program.parameter_schema);
        program.logical_layout_hash = program.parameter_schema.logical_layout_hash;
        return program;
    }

    toy3d::ShaderMapProgramData make_tonemap_program()
    {
        const toy3d::ShaderParametersMetadata& metadata =
            toy3d::tonemap_global_shader_type().parameter_metadata();
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/PostProcess/Tonemap";
        program.pass_name = "Tonemap";
        program.mapping_version = toy3d::shader::vulkan_binding_mapping_version;
        program.logical_layout_hash = nonzero_hash(70u);
        program.target_binding_hash = nonzero_hash(71u);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = toy3d::shader::default_shader_permutation_key;

        toy3d::ShaderMapBinding constants;
        constants.parameter_id = metadata.constant_buffer.binding_id;
        constants.name = metadata.constant_buffer.name;
        constants.group = toy3d::RHIBindingGroup::Pass;
        constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        constants.stages = toy3d::RHIShaderStageFlags::Pixel;
        constants.target_binding = 0u;
        constants.constant_buffer_size = metadata.constant_buffer.size;
        const toy3d::ShaderParameterConstantMemberMetadata& exposure = metadata.constant_buffer.members[0u];
        constants.constant_members.push_back({exposure.parameter_id, exposure.name,
                                              static_cast<toy3d::ShaderValueType>(exposure.type), exposure.offset,
                                              exposure.size, exposure.array_stride, exposure.matrix_stride});
        constants.data_layout_hash = metadata.constant_buffer.data_layout_hash;
        constants.shader_abi_version = metadata.constant_buffer.shader_abi_version;
        program.bindings.push_back(constants);

        toy3d::ShaderMapBinding texture;
        texture.parameter_id = metadata.resources[0u].parameter_id;
        texture.name = metadata.resources[0u].name;
        texture.group = toy3d::RHIBindingGroup::Pass;
        texture.type = toy3d::RHIResourceBindingType::SampledTexture;
        texture.stages = toy3d::RHIShaderStageFlags::Pixel;
        texture.target_binding = 1u;
        program.bindings.push_back(texture);

        toy3d::ShaderMapBinding sampler;
        sampler.parameter_id = metadata.resources[1u].parameter_id;
        sampler.name = metadata.resources[1u].name;
        sampler.group = toy3d::RHIBindingGroup::Pass;
        sampler.type = toy3d::RHIResourceBindingType::Sampler;
        sampler.stages = toy3d::RHIShaderStageFlags::Pixel;
        sampler.target_binding = 2u;
        program.bindings.push_back(sampler);

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {1u, 2u, 3u, 70u};
        vertex.content_hash = nonzero_hash(73u);
        program.stages.push_back(std::move(vertex));
        toy3d::ShaderMapStage pixel;
        pixel.stage = toy3d::RHIShaderStage::Pixel;
        pixel.entry_point = "ps_main";
        pixel.binary = {4u, 3u, 2u, 70u};
        pixel.content_hash = nonzero_hash(74u);
        pixel.reflection = program.bindings;
        program.stages.push_back(std::move(pixel));
        toy3d::tests::append_shader_parameters_metadata(metadata, program.parameter_schema);
        const toy3d::ViewShaderParameters view_parameters;
        const toy3d::ObjectShaderParameters object_parameters;
        toy3d::tests::append_shader_parameters_metadata(
            toy3d::shader_parameters_metadata(view_parameters), program.parameter_schema);
        toy3d::tests::append_shader_parameters_metadata(
            toy3d::shader_parameters_metadata(object_parameters), program.parameter_schema);
        std::sort(program.parameter_schema.constant_buffers.begin(), program.parameter_schema.constant_buffers.end(),
                  [](const toy3d::shader::ShaderParameterConstantBufferSchema& left,
                     const toy3d::shader::ShaderParameterConstantBufferSchema& right)
                  { return left.group < right.group; });
        program.parameter_schema.logical_layout_hash =
            toy3d::shader::calculate_shader_parameter_logical_layout_hash(program.parameter_schema);
        program.parameter_schema.schema_identity =
            toy3d::shader::calculate_shader_parameter_schema_identity(program.parameter_schema);
        program.logical_layout_hash = program.parameter_schema.logical_layout_hash;
        return program;
    }

    toy3d::ShaderMapProgramRef load_program(toy3d::ShaderMapProgramData program)
    {
        MaterialProgramLoader loader(program);
        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramKey key;
        key.shader_name = program.shader_name;
        key.pass_name = program.pass_name;
        key.platform = program.platform;
        key.permutation_key = program.permutation_key;
        toy3d::ShaderMapProgramResult result = shader_map.find_or_load(key);
        check(result.succeeded(), result.error.c_str());
        return result.program;
    }

    std::shared_ptr<const toy3d::GlobalShaderMap> load_tonemap_global_map()
    {
        toy3d::ShaderMapProgramData program = make_tonemap_program();
        MaterialProgramLoader loader(program);
        toy3d::ShaderMap shader_map(loader);
        toy3d::GlobalShaderMapResult result = toy3d::GlobalShaderMap::load(
            shader_map, toy3d::ShaderPlatform::VulkanES31, {&toy3d::tonemap_global_shader_type()});
        check(result.succeeded(), result.error.c_str());
        return result.shader_map;
    }
    toy3d::RHIResult<toy3d::RHIFrameEndResult> render_test_frame(
        toy3d::ForwardSceneRenderer& scene_renderer, toy3d::RenderScene& render_scene, toy3d::RHIDevice& device,
        toy3d::RenderResourceManager& resource_manager, toy3d::RHIViewportContext& viewport,
        toy3d::SceneRenderTargets& scene_render_targets, toy3d::TonemapPassResources& tonemap_pass_resources)
    {
        toy3d::RHIShaderProgramCache shader_program_cache(device);
        toy3d::ViewportOutputTarget viewport_output_target;
        return toy3d::render_viewport_frame(&scene_renderer, nullptr, toy3d::ViewportFrameOutput{},
                                            render_scene, device, shader_program_cache,
                                            resource_manager, viewport, scene_render_targets, tonemap_pass_resources,
                                            nullptr, viewport_output_target);
    }
    void test_material_schema_defaults()
    {
        using namespace toy3d;
        MaterialDesc desc;
        shader::ShaderParameterConstantBufferSchema buffer;
        buffer.binding_id = shader::make_shader_parameter_id(shader::BindingGroup::Material,
            shader::ShaderParameterCategory::Constant, "toy_material_data");
        buffer.name = "toy_material_data";
        buffer.group = shader::BindingGroup::Material;
        buffer.size = 48u;
        const std::vector<shader::ShaderValueType> types = {shader::ShaderValueType::Float32,
            shader::ShaderValueType::Float32x2, shader::ShaderValueType::Float32x3, shader::ShaderValueType::Float32x4};
        const std::vector<std::uint32_t> offsets = {0u, 4u, 16u, 32u};
        std::vector<shader::ReflectedConstantMember> reflected;
        for (std::uint32_t index = 0; index < types.size(); ++index)
        {
            shader::ShaderParameterConstantMemberSchema member;
            member.name = "value" + std::to_string(index);
            member.parameter_id = shader::make_shader_parameter_id(buffer.group,
                shader::ShaderParameterCategory::Constant, member.name);
            member.type = types[index];
            member.offset = offsets[index];
            member.size = (index + 1u) * 4u;
            // Binary32 little-endian 1.0; schema defaults must not use host padding.
            for (std::uint32_t component = 0; component <= index; ++component)
                member.default_value.insert(member.default_value.end(), {0u, 0u, 128u, 63u});
            reflected.push_back({member.parameter_id, member.name, member.type, member.offset, member.size, 0u, 0u});
            buffer.members.push_back(std::move(member));
        }
        buffer.data_layout_hash = shader::calculate_constant_buffer_data_layout_hash(buffer.group,
            buffer.binding_id, buffer.size, reflected);
        desc.parameter_schema.constant_buffers.push_back(buffer);
        desc.parameter_schema.logical_layout_hash = shader::calculate_shader_parameter_logical_layout_hash(desc.parameter_schema);
        desc.parameter_schema.schema_identity = shader::calculate_shader_parameter_schema_identity(desc.parameter_schema);
        std::string error;
        check(initialize_material_constant_defaults(desc, error) && desc.scalar_defaults.size() == 1u &&
              desc.vector2_defaults.size() == 1u && desc.vector3_defaults.size() == 1u && desc.vector4_defaults.size() == 1u &&
              desc.scalar_defaults.at(buffer.members[0].parameter_id) == 1.0f &&
              desc.vector4_defaults.at(buffer.members[3].parameter_id) == vec4(1.0f),
              "all scalar/vector schema defaults must decode without enumerating parameter names");
        auto damaged = desc;
        auto& member = damaged.parameter_schema.constant_buffers[0].members[3];
        member.default_value = {0u, 0u, 128u, 127u, 0u, 0u, 128u, 63u, 0u, 0u, 128u, 63u, 0u, 0u, 128u, 63u};
        damaged.parameter_schema.schema_identity = shader::calculate_shader_parameter_schema_identity(damaged.parameter_schema);
        damaged.scalar_defaults[buffer.members[0].parameter_id] = 3.0f;
        check(!initialize_material_constant_defaults(damaged, error) &&
              damaged.scalar_defaults.at(buffer.members[0].parameter_id) == 3.0f && damaged.vector4_defaults == desc.vector4_defaults,
              "non-finite schema defaults must fail without partially replacing runtime defaults");
        damaged = desc;
        damaged.parameter_schema.constant_buffers[0].members[1].default_value.clear();
        damaged.parameter_schema.schema_identity = shader::calculate_shader_parameter_schema_identity(damaged.parameter_schema);
        check(!initialize_material_constant_defaults(damaged, error), "missing defaults must not silently become zero");

    }
} // namespace

int main()
{
    test_material_schema_defaults();
    struct TestQueue final : toy3d::RHIQueue
    {
        using toy3d::RHIQueue::RHIQueue;

        toy3d::RHIQueueCompletionValue completed_value() const override { return 0; }

        toy3d::RHIStatus wait_for_value(toy3d::RHIQueueCompletionValue) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus wait_idle() override { return toy3d::RHIStatus::success(); }

      protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(const toy3d::RHISubmitInfo&) override
        {
            return toy3d::RHIResult<toy3d::RHISubmitResult>::failure(
                toy3d::RHIErrorCode::Unsupported, "The resource smoke does not submit command lists");
        }
    };

    struct : toy3d::RHIDevice
    {
        toy3d::RHIQueue* queue = nullptr;
        toy3d::RHICapabilities test_capabilities;
        toy3d::RHILimits test_limits;
        toy3d::RHITextureDesc last_texture_desc;
        std::vector<std::uint8_t> last_buffer_initial_data;
        std::uint32_t buffer_creation_count = 0;
        bool return_invalid_depth_view = false;
        std::vector<std::string>* operations = nullptr;

        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override { return toy3d::RHIStatus::success(); }

        const toy3d::RHICapabilities& capabilities() const override { return test_capabilities; }

        const toy3d::RHILimits& limits() const override { return test_limits; }

        toy3d::RHIFormatCapabilities format_capabilities(toy3d::PixelFormat) const override
        {
            toy3d::RHIFormatCapabilities result;
            result.usage = toy3d::RHIFormatUsage::Sampled | toy3d::RHIFormatUsage::Storage |
                           toy3d::RHIFormatUsage::RenderTarget | toy3d::RHIFormatUsage::DepthStencil |
                           toy3d::RHIFormatUsage::VertexBuffer | toy3d::RHIFormatUsage::CopySource |
                           toy3d::RHIFormatUsage::CopyDestination;
            return result;
        }

        toy3d::RHIQueue& graphics_queue() override { return *queue; }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>> create_viewport_context_impl(
            const toy3d::RHISurfaceRef&, const toy3d::RHIViewportContextDesc&) override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>::failure(
                toy3d::RHIErrorCode::Unsupported, "The resource smoke has no viewport");
        }

        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer_impl(const toy3d::RHIBufferDesc& desc,
                                                                 const toy3d::RHIInitialData* initial_data) override
        {
            ++buffer_creation_count;
            if (operations != nullptr)
            {
                operations->push_back("device_create");
            }
            last_buffer_initial_data.clear();
            if (initial_data != nullptr && initial_data->data != nullptr)
            {
                const auto* begin = static_cast<const std::uint8_t*>(initial_data->data);
                last_buffer_initial_data.assign(begin, begin + initial_data->size);
            }
            return toy3d::RHIResult<toy3d::RHIBufferRef>::success(std::make_shared<toy3d::RHIBuffer>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHITextureRef> create_texture_impl(const toy3d::RHITextureDesc& desc,
                                                                   const toy3d::RHIInitialData*) override
        {
            if (operations != nullptr)
            {
                operations->push_back("device_create");
            }
            last_texture_desc = desc;
            return toy3d::RHIResult<toy3d::RHITextureRef>::success(std::make_shared<toy3d::RHITexture>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view_impl(const toy3d::RHIBufferRef&,
                                                                          const toy3d::RHIBufferViewDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIBufferViewRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                      "The resource smoke creates no views");
        }

        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view_impl(
            const toy3d::RHITextureRef& texture, const toy3d::RHITextureViewDesc& desc) override
        {
            if (operations != nullptr)
            {
                operations->push_back("device_create");
            }
            toy3d::RHITextureViewDesc result_desc = desc;
            if (return_invalid_depth_view && desc.type == toy3d::RHIResourceViewType::DepthStencil)
            {
                result_desc.subresources.mip_count = 0u;
            }
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                std::make_shared<toy3d::RHITextureView>(texture, std::move(result_desc)));
        }

        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler_impl(const toy3d::RHISamplerDesc& desc) override
        {
            return toy3d::RHIResult<toy3d::RHISamplerRef>::success(std::make_shared<toy3d::RHISampler>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence_impl(const std::string&) override
        {
            return toy3d::RHIResult<toy3d::RHIGPUFenceRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                    "The resource smoke creates no fences");
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>> create_graphics_command_context_impl()
            override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::failure(
                toy3d::RHIErrorCode::Unsupported, "The resource smoke injects its command context");
        }

      protected:
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(const toy3d::RHIShaderDesc& desc) override
        {
            if (operations != nullptr)
            {
                operations->push_back("device_create");
            }
            return toy3d::RHIResult<toy3d::RHIShaderRef>::success(std::make_shared<toy3d::RHIShader>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIBindingLayoutRef> create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc& desc) override
        {
            if (operations != nullptr)
            {
                operations->push_back("device_create");
            }
            return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::success(
                std::make_shared<toy3d::RHIBindingLayout>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc& desc) override
        {
            if (operations != nullptr)
            {
                operations->push_back("device_create");
            }
            return toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>::success(
                std::make_shared<toy3d::RHIGraphicsPipeline>(*this, desc));
        }

        bool is_initialized_impl() const override { return true; }

        toy3d::RHIStatus wait_idle_before_shutdown_impl() override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus shutdown_impl() override { return toy3d::RHIStatus::success(); }
    } device;
    TestQueue queue(device);
    device.queue = &queue;
    device.test_capabilities.storage_resources = true;
    device.test_capabilities.indirect_draw = true;
    device.test_limits.max_color_attachments = std::numeric_limits<std::uint32_t>::max();
    device.test_limits.max_vertex_buffers = std::numeric_limits<std::uint32_t>::max();
    device.test_limits.max_texture_dimension_2d = std::numeric_limits<std::uint32_t>::max();
    device.test_limits.max_texture_array_layers = std::numeric_limits<std::uint32_t>::max();
    device.test_limits.max_uniform_buffer_size = std::numeric_limits<std::uint32_t>::max();
    device.test_limits.max_binding_slots_per_group = std::numeric_limits<std::uint32_t>::max();
    device.test_limits.max_sampler_anisotropy = std::numeric_limits<std::uint32_t>::max();
    const std::shared_ptr<const toy3d::GlobalShaderMap> global_shader_map = load_tonemap_global_map();
    toy3d::TonemapPassResources tonemap_resources;
    toy3d::RHIShaderProgramCache tonemap_program_cache(device);
    check(global_shader_map &&
              tonemap_resources.initialize(device, tonemap_program_cache, *global_shader_map).succeeded(),
          "frame-owner smoke requires initialized Tonemap resources");

    struct UniformContext final : toy3d::RHICommandContext
    {
        using toy3d::RHICommandContext::RHICommandContext;

        std::vector<std::uint8_t> last_buffer_upload_data;

        toy3d::RHIStatus begin_recording(const std::string&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus transition_resources_impl(const std::vector<toy3d::RHIResourceTransition>&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_buffer_impl(const toy3d::RHIBufferCopyDesc&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus upload_buffer_impl(const toy3d::RHIBufferUploadDesc& desc) override
        {
            const auto* begin = static_cast<const std::uint8_t*>(desc.source.data);
            last_buffer_upload_data.assign(begin, begin + desc.source.size);
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIResult<toy3d::RHIUniformBufferSlice> upload_transient_uniform_data_impl(
            const toy3d::RHITransientUniformDataDesc& desc) override
        {
            const auto* begin = static_cast<const std::uint8_t*>(desc.source.data);
            last_buffer_upload_data.assign(begin, begin + desc.source.size);
            toy3d::RHIBufferDesc buffer_desc;
            buffer_desc.size = desc.source.size;
            buffer_desc.usage = toy3d::RHIResourceUsage::UniformBuffer;
            toy3d::RHIUniformBufferSlice slice;
            slice.buffer = std::make_shared<toy3d::RHIBuffer>(*owner_device(), std::move(buffer_desc));
            slice.size = desc.source.size;
            return toy3d::RHIResult<toy3d::RHIUniformBufferSlice>::success(std::move(slice));
        }

        toy3d::RHIStatus copy_texture_impl(const toy3d::RHITextureCopyDesc&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus upload_texture_impl(const toy3d::RHITextureUploadDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus write_gpu_fence_impl(const toy3d::RHIGPUFenceRef&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIResult<toy3d::RHICommandListRef> finish_recording() override
        {
            return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "The ABI materialization smoke does not finish a command list");
        }
    } uniform_context(device);

    const toy3d::ViewShaderParameters view_parameters{toy3d::Matrix4(1.0f),
                                                       toy3d::Matrix4(2.0f),
                                                       toy3d::Matrix4(3.0f),
                                                       toy3d::Matrix4(4.0f),
                                                       toy3d::Matrix4(5.0f),
                                                       toy3d::Matrix4(6.0f),
                                                       toy3d::Vector3(7.0f, 8.0f, 9.0f),
                                                       toy3d::Vector3(10.0f, 11.0f, 12.0f)};
    const auto view_binding = toy3d::create_transient_shader_binding(device, uniform_context, view_parameters);
    float view_projection_diagonal = 0.0f;
    float camera_position_x = 0.0f;
    const bool view_bytes_complete = uniform_context.last_buffer_upload_data.size() == 416u;
    if (view_bytes_complete)
    {
        std::memcpy(&view_projection_diagonal, uniform_context.last_buffer_upload_data.data() + 128u, sizeof(float));
        std::memcpy(&camera_position_x, uniform_context.last_buffer_upload_data.data() + 384u, sizeof(float));
    }
    check(view_binding.succeeded() && view_binding.value() != nullptr &&
              view_binding.value()->group() == toy3d::RHIBindingGroup::View && view_bytes_complete &&
              view_projection_diagonal == 3.0f && camera_position_x == 7.0f &&
              uniform_context.last_buffer_upload_data[396u] == 0u,
          "View ABI materialization must write canonical members and keep padding zero");

    check(view_binding.succeeded() && view_binding.value()->desc().bindings.size() == 1u &&
              view_binding.value()->desc().bindings[0].binding_id != 0u &&
              view_binding.value()->desc().bindings[0].buffer != nullptr,
          "View binding must be logical identity based and independent of Program target slots");

    toy3d::Matrix4 object_to_world = toy3d::Matrix4::identity();
    object_to_world.at(3u, 0u) = 13.0f;
    toy3d::ObjectShaderParameters object_parameters;
    object_parameters.toy_object_to_world = object_to_world;
    object_parameters.toy_object_normal_to_world = toy3d::Matrix4::identity();
    object_parameters.toy_receives_shadows = 1.0f;
    const auto object_binding = toy3d::create_transient_shader_binding(device, uniform_context, object_parameters);
    float object_translation_x = 0.0f;
    float normal_diagonal = 0.0f;
    float receiver_flag = 0.0f;
    const bool object_bytes_complete = uniform_context.last_buffer_upload_data.size() ==
        sizeof(toy3d::Matrix4) * 2u + 16u;
    if (object_bytes_complete)
    {
        std::memcpy(&object_translation_x, uniform_context.last_buffer_upload_data.data() + 48u, sizeof(float));
        std::memcpy(&normal_diagonal, uniform_context.last_buffer_upload_data.data() + 64u, sizeof(float));
        std::memcpy(&receiver_flag, uniform_context.last_buffer_upload_data.data() + 128u, sizeof(float));
    }
    check(object_binding.succeeded() && object_binding.value() != nullptr &&
              object_binding.value()->group() == toy3d::RHIBindingGroup::Object && object_bytes_complete &&
              object_translation_x == 13.0f && normal_diagonal == 1.0f && receiver_flag == 1.0f,
          "Object ABI materialization must write matrices and the receiver flag");

    struct GraphicsContext final : toy3d::RHIGraphicsCommandContext
    {
        using toy3d::RHIGraphicsCommandContext::RHIGraphicsCommandContext;

        std::uint32_t transition_count = 0;
        std::uint32_t upload_count = 0;
        std::uint32_t texture_upload_count = 0;
        std::vector<std::uint8_t> last_buffer_upload_data;
        std::vector<std::string>* operations = nullptr;
        std::uint32_t* view_uniform_upload_count = nullptr;
        std::uint32_t* object_uniform_upload_count = nullptr;
        toy3d::RHIDevice* command_device = nullptr;
        bool finish_success = false;
        bool fail_draw_indexed = false;
        bool fail_view_uniform_upload = false;
        bool fail_next_material_uniform_upload = false;

        toy3d::RHIStatus begin_recording(const std::string&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("begin_recording");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus transition_resources_impl(const std::vector<toy3d::RHIResourceTransition>& transitions) override
        {
            transition_count += static_cast<std::uint32_t>(transitions.size());
            if (operations != nullptr)
            {
                operations->push_back("transition");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_buffer_impl(const toy3d::RHIBufferCopyDesc&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus upload_buffer_impl(const toy3d::RHIBufferUploadDesc& desc) override
        {
            ++upload_count;
            if (view_uniform_upload_count != nullptr && desc.source.size == 416u)
            {
                ++(*view_uniform_upload_count);
            }
            if (object_uniform_upload_count != nullptr && desc.source.size == sizeof(toy3d::Matrix4) * 2u + 16u)
            {
                ++(*object_uniform_upload_count);
            }
            if (fail_view_uniform_upload && desc.source.size == 416u)
            {
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::Unsupported,
                                                 "Injected View uniform upload failure");
            }
            if (fail_next_material_uniform_upload && desc.source.size == 16u)
            {
                fail_next_material_uniform_upload = false;
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::Unsupported,
                                                 "Injected first Material uniform upload failure");
            }
            if (operations != nullptr)
            {
                operations->push_back("upload_buffer");
            }
            const auto* begin = static_cast<const std::uint8_t*>(desc.source.data);
            last_buffer_upload_data.assign(begin, begin + desc.source.size);
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIResult<toy3d::RHIUniformBufferSlice> upload_transient_uniform_data_impl(
            const toy3d::RHITransientUniformDataDesc& desc) override
        {
            ++upload_count;
            if (view_uniform_upload_count != nullptr && desc.source.size == 416u)
            {
                ++(*view_uniform_upload_count);
            }
            if (object_uniform_upload_count != nullptr && desc.source.size == sizeof(toy3d::Matrix4) * 2u + 16u)
            {
                ++(*object_uniform_upload_count);
            }
            if (fail_view_uniform_upload && desc.source.size == 416u)
            {
                return toy3d::RHIResult<toy3d::RHIUniformBufferSlice>::failure(
                    toy3d::RHIErrorCode::Unsupported, "Injected View uniform upload failure");
            }
            if (fail_next_material_uniform_upload && desc.source.size == 16u)
            {
                fail_next_material_uniform_upload = false;
                return toy3d::RHIResult<toy3d::RHIUniformBufferSlice>::failure(
                    toy3d::RHIErrorCode::Unsupported, "Injected first Material uniform upload failure");
            }
            if (operations != nullptr)
            {
                operations->push_back("upload_transient_uniform");
            }
            const auto* begin = static_cast<const std::uint8_t*>(desc.source.data);
            last_buffer_upload_data.assign(begin, begin + desc.source.size);
            toy3d::RHIBufferDesc buffer_desc;
            buffer_desc.size = desc.source.size;
            buffer_desc.usage = toy3d::RHIResourceUsage::UniformBuffer;
            toy3d::RHIUniformBufferSlice slice;
            slice.buffer = std::make_shared<toy3d::RHIBuffer>(*owner_device(), std::move(buffer_desc));
            slice.size = desc.source.size;
            return toy3d::RHIResult<toy3d::RHIUniformBufferSlice>::success(std::move(slice));
        }

        toy3d::RHIStatus copy_texture_impl(const toy3d::RHITextureCopyDesc&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("copy_texture");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_texture_impl(const toy3d::RHITextureUploadDesc&) override
        {
            ++texture_upload_count;
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus write_gpu_fence_impl(const toy3d::RHIGPUFenceRef&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIResult<toy3d::RHICommandListRef> finish_recording() override
        {
            if (finish_success && command_device != nullptr)
            {
                if (operations != nullptr)
                {
                    operations->push_back("finish_recording");
                }
                return toy3d::RHIResult<toy3d::RHICommandListRef>::success(
                    std::make_shared<toy3d::RHICommandList>(*command_device));
            }
            return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "The manager smoke does not finish an RHI command list");
        }

        toy3d::RHIStatus begin_render_pass_impl(const toy3d::RHIRenderPassDesc&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("begin_render_pass");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus end_render_pass() override
        {
            if (operations != nullptr)
            {
                operations->push_back("end_render_pass");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_graphics_pipeline_impl(const toy3d::RHIGraphicsPipelineRef&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("set_graphics_pipeline");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_viewport(const toy3d::RHIViewport&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus set_scissor(const toy3d::RHIRect&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus set_blend_constants(const toy3d::vec4&) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus set_stencil_reference(std::uint8_t) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus set_vertex_buffers_impl(const std::vector<toy3d::RHIVertexBufferBinding>&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_index_buffer_impl(const toy3d::RHIIndexBufferBinding&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus draw(const toy3d::RHIDrawArgs&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("draw");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus draw_indexed(const toy3d::RHIDrawIndexedArgs&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("draw_indexed");
            }
            if (fail_draw_indexed)
            {
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::BackendFailure,
                                                 "Injected Base Pass draw failure");
            }
            return toy3d::RHIStatus::success();
        }

      protected:
        toy3d::RHIStatus bind_graphics_bindings_impl(const toy3d::RHIGraphicsBindings&) override
        {
            return toy3d::RHIStatus::success();
        }
    } context(device);

    struct : toy3d::RenderResource
    {
        int record_count = 0;
        int commit_count = 0;
        int discard_count = 0;
        int release_count = 0;
        bool retryable_failure = false;
        bool deterministic_failure = false;
        std::vector<std::string>* operations = nullptr;

        toy3d::RHIStatus record_upload(toy3d::RHIDevice&, toy3d::RHIGraphicsCommandContext&) override
        {
            ++record_count;
            if (operations != nullptr)
            {
                operations->push_back("record_pending_uploads");
            }
            if (deterministic_failure)
            {
                return fail(
                    toy3d::RHIStatus::failure(toy3d::RHIErrorCode::Unsupported, "Deterministic test resource failure"));
            }
            if (retryable_failure)
            {
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::BackendFailure,
                                                 "Retryable test recording failure");
            }
            return toy3d::RHIStatus::success();
        }

        void on_recording_committed() noexcept override
        {
            ++commit_count;
            if (operations != nullptr)
            {
                operations->push_back("commit_recording");
            }
        }

        void on_recording_discarded() noexcept override
        {
            ++discard_count;
            if (operations != nullptr)
            {
                operations->push_back("discard_recording");
            }
        }

        void release_rhi() noexcept override { ++release_count; }
    } ready_first, ready_second, retry_first, retry_second, deterministic, released_while_recording, terminal_first,
        terminal_second;

    toy3d::RenderResourceManager manager(device);
    check(manager.begin_init(ready_first) && manager.begin_init(ready_second),
          "begin_init must collect uninitialized resources as non-owning pending entries");
    check(manager.record_pending_uploads(context).succeeded(),
          "record_pending_uploads must record every stable pending resource");
    check(ready_first.state() == toy3d::RenderResourceState::PendingUpload &&
              ready_second.state() == toy3d::RenderResourceState::PendingUpload,
          "successful recording must not publish long-term Ready before submit commit");
    check(manager.commit_recording().succeeded(),
          "business submit success must commit the complete recording collection");
    check(ready_first.state() == toy3d::RenderResourceState::Ready &&
              ready_second.state() == toy3d::RenderResourceState::Ready && ready_first.commit_count == 1 &&
              ready_second.commit_count == 1,
          "commit must atomically publish all recorded resources Ready");
    check(manager.release(ready_first) && manager.release(ready_second),
          "Ready resources must release RHI refs without waiting for GPU completion");

    retry_second.retryable_failure = true;
    check(manager.begin_init(retry_first) && manager.begin_init(retry_second),
          "retry resources must enter PendingUpload");
    const toy3d::RHIStatus recording_failure = manager.record_pending_uploads(context);
    check(!recording_failure && retry_first.state() == toy3d::RenderResourceState::PendingUpload &&
              retry_second.state() == toy3d::RenderResourceState::PendingUpload,
          "a retryable recording failure must keep every resource PendingUpload");
    check(!manager.commit_recording(), "a failed recording must not be commit eligible");
    check(manager.discard_recording() && retry_first.discard_count == 1 && retry_second.discard_count == 1,
          "discard must clear successful and partially recorded resource candidates");
    retry_second.retryable_failure = false;
    check(manager.record_pending_uploads(context) && manager.commit_recording(),
          "discarded PendingUpload resources must be recordable on a later valid frame");
    check(retry_first.state() == toy3d::RenderResourceState::Ready &&
              retry_second.state() == toy3d::RenderResourceState::Ready,
          "retry submit commit must publish both resources Ready");
    check(manager.release(retry_first) && manager.release(retry_second),
          "retried resources must support normal release");

    toy3d::MaterialDesc material_desc;
    material_desc.shader_name = "StaticMeshResourceSmoke";
    const toy3d::MaterialRef material = toy3d::Material::create(material_desc);
    const toy3d::MaterialInstanceRef material_instance = toy3d::MaterialInstance::create(material);
    toy3d::StaticMeshDesc mesh_desc;
    mesh_desc.vertices = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                          {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                          {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
    mesh_desc.vertex_colors = {{255u, 0u, 0u, 255u}, {0u, 255u, 0u, 255u}, {0u, 0u, 255u, 255u}};
    // The mesh owns one explicit index width; UInt16 is sufficient for this
    // triangle and lets the smoke verify the matching RHI binding format.
    mesh_desc.indices = std::vector<std::uint16_t>{0u, 1u, 2u};
    mesh_desc.sections.push_back({0u, 3u, 0u});
    mesh_desc.material_slots.push_back(material_instance);
    const toy3d::StaticMeshRef static_mesh = toy3d::StaticMesh::create(std::move(mesh_desc));
    check(static_mesh != nullptr, "the StaticMesh resource smoke requires a valid immutable Asset");

    toy3d::StaticMeshRenderData render_data(*static_mesh);
    check(render_data.begin_init(manager).succeeded(),
          "StaticMeshRenderData begin_init must enqueue its complete buffer candidate");
    const std::uint32_t transitions_before_mesh = context.transition_count;
    const std::uint32_t uploads_before_mesh = context.upload_count;
    check(manager.record_pending_uploads(context).succeeded() && render_data.prepare_current_recording().succeeded() &&
              render_data.is_drawable(),
          "all mesh uploads and LocalVertexFactory validation must open the current-list candidate gate");
    check(context.transition_count - transitions_before_mesh == 8u && context.upload_count - uploads_before_mesh == 4u,
          "four mesh buffers must each record two transitions and one upload");
    check(manager.discard_recording().succeeded() && !render_data.is_drawable(),
          "discard must close the complete mesh gate and retain payload for retry");

    check(manager.record_pending_uploads(context).succeeded() && render_data.prepare_current_recording().succeeded() &&
              manager.commit_recording().succeeded() && render_data.is_drawable(),
          "a later valid submit commit must publish every mesh buffer Ready together");
    const toy3d::RHIIndexBufferBinding index_binding = render_data.index_buffer_binding();
    check(index_binding.buffer != nullptr && index_binding.format == toy3d::RHIIndexFormat::UInt16,
          "StaticMeshIndexBuffer must retain the Asset index-width contract");
    toy3d::MaterialRenderProxy batch_material_proxy(*material);
    toy3d::StaticMeshSceneProxy batch_scene_proxy(toy3d::Matrix4::identity(), static_mesh->local_bounds(), true,
                                                  &render_data, {&batch_material_proxy});
    const toy3d::StaticMeshSection& batch_section = render_data.sections().front();
    toy3d::MeshBatch mesh_batch(batch_scene_proxy, render_data, *render_data.vertex_factory(), batch_material_proxy,
                                batch_section.first_index, batch_section.index_count);
    check(&mesh_batch.scene_proxy() == &batch_scene_proxy && &mesh_batch.render_data() == &render_data &&
              &mesh_batch.vertex_factory() == render_data.vertex_factory() &&
              &mesh_batch.material_render_proxy() == &batch_material_proxy && mesh_batch.first_index() == 0u &&
              mesh_batch.index_count() == 3u,
          "MeshBatch must compose one frame-local section with non-owning Proxy, RenderData, LocalVertexFactory, and "
          "MaterialRenderProxy references");

    std::vector<toy3d::ShaderVertexInput> shader_inputs(4u);
    shader_inputs[0].attribute_id = toy3d::ShaderVertexAttributeId::Position0;
    shader_inputs[0].component_count = 4u;
    shader_inputs[0].target_location = 0u;
    shader_inputs[1].attribute_id = toy3d::ShaderVertexAttributeId::Normal0;
    shader_inputs[1].component_count = 4u;
    shader_inputs[1].target_location = 1u;
    shader_inputs[2].attribute_id = toy3d::ShaderVertexAttributeId::TexCoord0;
    shader_inputs[2].component_count = 2u;
    shader_inputs[2].target_location = 2u;
    shader_inputs[3].attribute_id = toy3d::ShaderVertexAttributeId::Color0;
    shader_inputs[3].component_count = 4u;
    shader_inputs[3].target_location = 3u;
    std::vector<toy3d::RHIGraphicsPipelineDesc::VertexBufferLayout> layouts;
    std::vector<toy3d::RHIGraphicsPipelineDesc::VertexAttribute> attributes;
    std::vector<toy3d::RHIVertexBufferBinding> bindings;
    check(render_data.vertex_factory() != nullptr &&
              render_data.vertex_factory()
                  ->build_vertex_input(shader_inputs, layouts, attributes, bindings)
                  .succeeded() &&
              layouts.size() == 3u && attributes.size() == 4u && bindings.size() == 3u,
          "LocalVertexFactory must match fixed geometry streams including optional COLOR0 without selecting a Shader");
    check(render_data.release(manager).succeeded() && !render_data.is_drawable(),
          "releasing StaticMeshRenderData must close its complete drawable gate");

    toy3d::StaticMeshDesc uncolored_desc;
    uncolored_desc.vertices = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                               {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                               {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
    // Exercise the other fixed index-width alternative while leaving COLOR0
    // absent, which is a valid part of the first-stage stream contract.
    uncolored_desc.indices = std::vector<std::uint32_t>{0u, 1u, 2u};
    uncolored_desc.sections.push_back({0u, 3u, 0u});
    uncolored_desc.material_slots.push_back(material_instance);
    const toy3d::StaticMeshRef uncolored_mesh = toy3d::StaticMesh::create(std::move(uncolored_desc));
    check(uncolored_mesh != nullptr, "StaticMesh must allow the optional color stream to be absent");
    toy3d::StaticMeshRenderData uncolored_render_data(*uncolored_mesh);
    check(uncolored_render_data.begin_init(manager).succeeded() &&
              manager.record_pending_uploads(context).succeeded() &&
              uncolored_render_data.prepare_current_recording().succeeded() && manager.commit_recording().succeeded(),
          "an uncolored mesh candidate must initialize as three required buffers");
    shader_inputs.resize(3u);
    check(uncolored_render_data.vertex_factory()
              ->build_vertex_input(shader_inputs, layouts, attributes, bindings)
              .succeeded(),
          "an uncolored LocalVertexFactory must match a Shader that does not require COLOR0");
    shader_inputs.resize(4u);
    shader_inputs[3].attribute_id = toy3d::ShaderVertexAttributeId::Color0;
    shader_inputs[3].component_count = 4u;
    shader_inputs[3].target_location = 3u;
    check(!uncolored_render_data.vertex_factory()->build_vertex_input(shader_inputs, layouts, attributes, bindings),
          "an uncolored LocalVertexFactory must reject a Shader that requires COLOR0");
    check(uncolored_render_data.index_buffer_binding().format == toy3d::RHIIndexFormat::UInt32 &&
              uncolored_render_data.release(manager).succeeded(),
          "the uncolored candidate must preserve UInt32 indices and release normally");

    toy3d::TextureDesc invalid_texture_desc;
    invalid_texture_desc.width = 4;
    invalid_texture_desc.height = 4;
    invalid_texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    invalid_texture_desc.row_pitches = {17u};
    invalid_texture_desc.slice_pitches = {68u};
    invalid_texture_desc.mip_pixels = {std::vector<std::uint8_t>(68u, 0u)};
    check(toy3d::Texture::create(std::move(invalid_texture_desc)) == nullptr,
          "TextureDesc validation must reject a row pitch that splits PixelFormat blocks");

    toy3d::TextureDesc texture_desc;
    texture_desc.width = 4;
    texture_desc.height = 4;
    texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    texture_desc.row_pitches = {16u};
    texture_desc.slice_pitches = {64u};
    texture_desc.mip_pixels = {std::vector<std::uint8_t>(64u, 255u)};
    const toy3d::TextureRef texture = toy3d::Texture::create(std::move(texture_desc));
    check(texture != nullptr && texture->texture_resource() != nullptr,
          "a valid Texture must own a stable TextureResource allocation");
    toy3d::TextureResource* const texture_resource = texture->texture_resource();
    check(texture_resource->begin_init(manager).succeeded(),
          "TextureResource initial payload must enter the shared manager");
    const std::uint32_t texture_transitions_before = context.transition_count;
    check(
        manager.record_pending_uploads(context).succeeded() && context.texture_upload_count == 1u &&
            context.transition_count - texture_transitions_before == 2u &&
            texture_resource->view_for_current_recording() != nullptr && texture_resource->binding_generation() == 0u,
        "Texture initial upload must create a current-list view and record copy transitions without early publication");
    check(device.last_texture_desc.sample_count == 1u &&
              toy3d::EnumHasAllFlags(device.last_texture_desc.usage, toy3d::RHIResourceUsage::ShaderResource |
                                                                         toy3d::RHIResourceUsage::CopyDestination),
          "TextureResource must apply the fixed sampled and copy-destination RHI policy");
    check(manager.discard_recording().succeeded() && texture_resource->view_for_current_recording() == nullptr,
          "discarded initial Texture upload must retain CPU payload without publishing a view");
    check(manager.record_pending_uploads(context).succeeded() && manager.commit_recording().succeeded() &&
              texture_resource->active_view() != nullptr && texture_resource->binding_generation() == 1u,
          "submit commit must publish the first Texture view and nonzero binding generation");

    toy3d::ThreadManager material_thread_manager;
    toy3d::TaskGraphCreateResult material_graph_result =
        toy3d::create_task_graph({0u, 256u, false}, material_thread_manager);
    check(material_graph_result.succeeded(), "Material fixture must create a single-thread Task Graph");
    std::unique_ptr<toy3d::TaskGraphInterface> material_graph = material_graph_result.take_task_graph();
    check(material_graph != nullptr && material_graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
          "Material fixture must attach its Game Thread");
    toy3d::RenderingThread material_rendering_thread(material_thread_manager, *material_graph,
                                                     toy3d::RenderingThreadMode::SingleThread);
    check(material_rendering_thread.start().succeeded(), "Material fixture must open the RenderCommand facade");

    toy3d::RenderResourceManager frame_manager(device);
    std::unique_ptr<toy3d::RenderScene> frame_render_scene =
        std::make_unique<toy3d::RenderScene>(*material_graph, frame_manager);
    toy3d::RHITextureDesc present_texture_desc;
    present_texture_desc.width = 64u;
    present_texture_desc.height = 64u;
    present_texture_desc.format = toy3d::PixelFormat::B8G8R8A8UNorm;
    present_texture_desc.usage = toy3d::RHIResourceUsage::RenderTarget | toy3d::RHIResourceUsage::CopyDestination;
    present_texture_desc.initial_access = toy3d::RHIAccess::Present;
    const toy3d::RHITextureRef present_texture = std::make_shared<toy3d::RHITexture>(device, present_texture_desc);
    toy3d::RHITextureViewDesc present_view_desc;
    present_view_desc.type = toy3d::RHIResourceViewType::RenderTarget;
    present_view_desc.format = present_texture_desc.format;
    present_view_desc.subresources.mip_count = 1u;
    present_view_desc.subresources.layer_count = 1u;
    const toy3d::RHITextureViewRef present_view =
        std::make_shared<toy3d::RHITextureView>(present_texture, present_view_desc);

    struct : toy3d::RHIFrameContext
    {
        using toy3d::RHIFrameContext::RHIFrameContext;

        toy3d::RHITextureRef color_texture;
        toy3d::RHITextureViewRef color_view;
        std::unique_ptr<toy3d::RHIGraphicsCommandContext> commands;
        toy3d::Extent frame_extent{64u, 64u};

        const toy3d::RHITextureRef& present_texture() const override { return color_texture; }

        const toy3d::RHITextureViewRef& present_view() const override { return color_view; }

        toy3d::Extent extent() const override { return frame_extent; }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>
        create_graphics_command_context_impl() override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::success(std::move(commands));
        }
    } frame_context_shape(device);

    struct FrameViewport final : toy3d::RHIViewportContext
    {
        using toy3d::RHIViewportContext::RHIViewportContext;

        std::unique_ptr<toy3d::RHIFrameContext> next_frame;
        std::vector<std::string>* operations = nullptr;
        std::uint32_t* view_uniform_upload_count = nullptr;
        std::uint32_t* object_uniform_upload_count = nullptr;
        std::uint32_t begin_count = 0u;
        std::uint32_t end_count = 0u;
        std::uint32_t abort_count = 0u;
        bool end_success = true;
        bool fail_next_material_uniform_upload = false;
        toy3d::RHIQueueCompletionValue next_completion_value = 42u;
        toy3d::RHIStatus next_presentation_status = toy3d::RHIStatus::success();

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>> begin_frame_impl() override
        {
            ++begin_count;
            if (operations != nullptr)
            {
                operations->push_back("begin_frame");
            }
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>>::success(std::move(next_frame));
        }

        toy3d::RHIResult<toy3d::RHIFrameEndResult> end_frame(
            std::unique_ptr<toy3d::RHIFrameContext> frame,
            const std::vector<toy3d::RHICommandListRef>& command_lists) override
        {
            ++end_count;
            if (operations != nullptr)
            {
                operations->push_back("end_frame");
            }
            if (!frame || command_lists.size() != 1u || !command_lists.front())
            {
                return toy3d::RHIResult<toy3d::RHIFrameEndResult>::failure(
                    toy3d::RHIErrorCode::InvalidArgument, "Frame-owner smoke requires one immutable business list");
            }
            if (!end_success)
            {
                return toy3d::RHIResult<toy3d::RHIFrameEndResult>::failure(toy3d::RHIErrorCode::BackendFailure,
                                                                           "Injected frame-owner submit failure");
            }
            toy3d::RHIFrameEndResult result;
            result.completion_value = next_completion_value;
            result.presentation_status = next_presentation_status;
            return toy3d::RHIResult<toy3d::RHIFrameEndResult>::success(std::move(result));
        }

        toy3d::RHIStatus abort_frame(std::unique_ptr<toy3d::RHIFrameContext> frame) override
        {
            ++abort_count;
            if (operations != nullptr)
            {
                operations->push_back("abort_frame");
            }
            return frame ? toy3d::RHIStatus::success()
                         : toy3d::RHIStatus::failure(toy3d::RHIErrorCode::InvalidArgument,
                                                     "Frame-owner smoke cannot abort an empty frame");
        }

        toy3d::RHIStatus request_resize(const toy3d::Extent&) override { return toy3d::RHIStatus::success(); }
    } frame_viewport(device);

    const auto make_frame = [&](bool fail_draw_indexed = false,
                                bool fail_view_uniform_upload = false) -> std::unique_ptr<toy3d::RHIFrameContext>
    {
        auto frame_context = std::make_unique<decltype(frame_context_shape)>(device);
        frame_context->color_texture = present_texture;
        frame_context->color_view = present_view;
        auto frame_commands = std::make_unique<decltype(context)>(device);
        frame_commands->operations = frame_viewport.operations;
        frame_commands->view_uniform_upload_count = frame_viewport.view_uniform_upload_count;
        frame_commands->object_uniform_upload_count = frame_viewport.object_uniform_upload_count;
        frame_commands->command_device = &device;
        frame_commands->finish_success = true;
        frame_commands->fail_draw_indexed = fail_draw_indexed;
        frame_commands->fail_view_uniform_upload = fail_view_uniform_upload;
        frame_commands->fail_next_material_uniform_upload = frame_viewport.fail_next_material_uniform_upload;
        frame_context->commands = std::move(frame_commands);
        return frame_context;
    };
    const auto make_valid_view_family = [&]()
    {
        std::vector<toy3d::SceneView> views;
        views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                           toy3d::IntRect{0, 0, 64u, 64u}, toy3d::Extent{64u, 64u},
                           toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 100.0f);
        return toy3d::SceneViewFamily(*frame_render_scene, toy3d::Extent{64u, 64u}, std::move(views));
    };

    decltype(ready_first) submitted_frame_resource;
    std::vector<std::string> submitted_frame_operations;
    submitted_frame_resource.operations = &submitted_frame_operations;
    frame_viewport.operations = &submitted_frame_operations;
    frame_viewport.next_frame = make_frame();
    check(frame_manager.begin_init(submitted_frame_resource).succeeded(),
          "frame-owner smoke must begin a pending resource transaction");
    std::vector<toy3d::SceneView> submitted_views;
    submitted_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                                 toy3d::IntRect{0, 0, 64u, 64u}, toy3d::Extent{64u, 64u},
                                 toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 100.0f);
    toy3d::ForwardSceneRenderer submitted_frame_renderer(
        toy3d::SceneViewFamily(*frame_render_scene, toy3d::Extent{64u, 64u}, std::move(submitted_views)));
    toy3d::SceneRenderTargets submitted_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> submitted_frame_result =
        render_test_frame(submitted_frame_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          submitted_scene_render_targets, tonemap_resources);
    const std::vector<std::string> expected_submitted_operations = {"begin_frame",
                                                                    "begin_recording",
                                                                    "record_pending_uploads",
                                                                    "transition",
                                                                    "begin_render_pass",
                                                                    "end_render_pass",
                                                                    "transition",
                                                                    "transition",
                                                                    "begin_render_pass",
                                                                    "end_render_pass",
                                                                    "transition",
                                                                    "transition",
                                                                    "begin_render_pass",
                                                                    "end_render_pass",
                                                                    "transition",
                                                                    "upload_transient_uniform",
                                                                    "begin_render_pass",
                                                                    "set_graphics_pipeline",
                                                                    "draw",
                                                                    "end_render_pass",
                                                                    "transition",
                                                                    "finish_recording",
                                                                    "end_frame",
                                                                    "commit_recording"};
    check(submitted_frame_result.succeeded() && submitted_frame_result.value().completion_value == 42u &&
              submitted_frame_resource.state() == toy3d::RenderResourceState::Ready &&
              submitted_scene_render_targets.scene_color_texture() != nullptr &&
              submitted_scene_render_targets.scene_color_access() == toy3d::RHIAccess::ShaderResourceGraphics &&
              submitted_scene_render_targets.scene_depth_access() == toy3d::RHIAccess::DepthStencilWrite &&
              submitted_frame_operations == expected_submitted_operations && frame_viewport.end_count == 1u &&
              frame_viewport.abort_count == 0u,
          "frame owner must record uploads and Base Pass in one list, then commit only after submit");
    check(frame_manager.release(submitted_frame_resource).succeeded(),
          "submitted frame resource must release after the frame-owner smoke");

    std::vector<std::string> embedded_frame_operations;
    frame_viewport.operations = &embedded_frame_operations;
    device.operations = &embedded_frame_operations;
    FrameViewport embedded_viewport(device);
    embedded_viewport.operations = &embedded_frame_operations;
    embedded_viewport.next_frame = make_frame();
    std::vector<toy3d::SceneView> embedded_views;
    embedded_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(),
                                toy3d::Vector3(0.0f, 0.0f, 1.0f), toy3d::IntRect{0, 0, 32u, 32u},
                                toy3d::Extent{32u, 32u}, toy3d::CameraProjectionMode::Perspective,
                                toy3d::Radians(1.0f), 0.1f, 100.0f);
    toy3d::ForwardSceneRenderer embedded_renderer(
        toy3d::SceneViewFamily(*frame_render_scene, toy3d::Extent{32u, 32u}, std::move(embedded_views)));
    toy3d::SceneRenderTargets embedded_scene_targets;
    toy3d::ViewportOutputTarget embedded_output_target;
    toy3d::ViewportFrameOutput embedded_output;
    embedded_output.sample_in_ui = true;
    embedded_output.window_extent = {64u, 64u};
    embedded_output.scene_extent = {32u, 32u};
    embedded_output.texture_id = toy3d::IMGUI_SCENE_VIEWPORT_TEXTURE_ID;
    toy3d::RHIShaderProgramCache embedded_shader_cache(device);
    const auto embedded_result = toy3d::render_viewport_frame(
        &embedded_renderer, nullptr, embedded_output, *frame_render_scene, device, embedded_shader_cache,
        frame_manager, embedded_viewport, embedded_scene_targets, tonemap_resources, nullptr, embedded_output_target);
    check(embedded_result.succeeded() && embedded_scene_targets.scene_color_texture() &&
              embedded_scene_targets.scene_color_texture()->desc().width == 32u &&
              embedded_output_target.texture() && embedded_output_target.texture()->desc().width == 32u &&
              embedded_output_target.access() == toy3d::RHIAccess::ShaderResourceGraphics &&
              embedded_viewport.end_count == 1u,
          "embedded viewport must render at panel extent and publish an SDR texture before window presentation");

    FrameViewport hidden_viewport(device);
    hidden_viewport.operations = &embedded_frame_operations;
    hidden_viewport.next_frame = make_frame();
    toy3d::ViewportFrameOutput hidden_output = embedded_output;
    hidden_output.scene_extent = {};
    toy3d::SceneRenderTargets hidden_scene_targets;
    toy3d::ViewportOutputTarget hidden_output_target;
    const auto hidden_result = toy3d::render_viewport_frame(
        nullptr, nullptr, hidden_output, *frame_render_scene, device, embedded_shader_cache, frame_manager,
        hidden_viewport, hidden_scene_targets, tonemap_resources, nullptr, hidden_output_target);
    check(hidden_result.succeeded() && !hidden_scene_targets.scene_color_texture() &&
              !hidden_output_target.texture() && hidden_viewport.end_count == 1u,
          "hidden embedded viewport must present UI without allocating a scene target");

    const toy3d::ShaderMapProgramRef base_pass_program = load_program(make_base_pass_program());
    toy3d::MaterialDesc base_pass_material_desc;
    base_pass_material_desc.shader_name = "Toy3d/Test/ViewObject";
    base_pass_material_desc.shader_program = base_pass_program;
    const toy3d::MaterialRef base_pass_material = toy3d::Material::create(std::move(base_pass_material_desc));
    toy3d::MaterialInstanceRef base_pass_material_instance = toy3d::MaterialInstance::create(base_pass_material);
    toy3d::StaticMeshDesc base_pass_mesh_desc;
    base_pass_mesh_desc.vertices = {{{-0.5f, -0.5f, 4.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                                    {{0.5f, -0.5f, 4.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                                    {{0.0f, 0.5f, 4.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
    base_pass_mesh_desc.indices = std::vector<std::uint16_t>{0u, 1u, 2u};
    base_pass_mesh_desc.sections.push_back({0u, 3u, 0u});
    base_pass_mesh_desc.sections.push_back({0u, 3u, 0u});
    base_pass_mesh_desc.material_slots.push_back(base_pass_material_instance);
    const toy3d::StaticMeshRef base_pass_mesh = toy3d::StaticMesh::create(std::move(base_pass_mesh_desc));
    toy3d::StaticMeshRenderData base_pass_render_data(*base_pass_mesh);
    check(base_pass_render_data.begin_init(frame_manager).succeeded() &&
              frame_manager.record_pending_uploads(context).succeeded() &&
              base_pass_render_data.prepare_current_recording().succeeded() &&
              frame_manager.commit_recording().succeeded(),
          "Base Pass operation-order fixture must publish drawable mesh buffers");

    auto base_pass_material_proxy = std::make_unique<toy3d::MaterialRenderProxy>(*base_pass_material);

    std::unique_ptr<toy3d::RenderScene> base_pass_scene =
        std::make_unique<toy3d::RenderScene>(*material_graph, frame_manager);
    base_pass_scene->add_primitive(std::make_unique<toy3d::StaticMeshSceneProxy>(
        toy3d::Matrix4::identity(), base_pass_mesh->local_bounds(), true, &base_pass_render_data,
        std::vector<toy3d::MaterialRenderProxy*>{base_pass_material_proxy.get()}));

    const auto make_base_pass_view_family = [&]()
    {
        std::vector<toy3d::SceneView> views;
        views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                           toy3d::IntRect{0, 0, 64u, 64u}, toy3d::Extent{64u, 64u},
                           toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 100.0f);
        views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                           toy3d::IntRect{0, 0, 64u, 64u}, toy3d::Extent{64u, 64u},
                           toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 100.0f);
        return toy3d::SceneViewFamily(*base_pass_scene, toy3d::Extent{64u, 64u}, std::move(views));
    };

    decltype(ready_first) base_pass_draw_resource;
    std::vector<std::string> base_pass_draw_operations;
    base_pass_draw_resource.operations = &base_pass_draw_operations;
    frame_viewport.operations = &base_pass_draw_operations;
    device.operations = &base_pass_draw_operations;
    std::uint32_t base_pass_view_uniform_upload_count = 0u;
    std::uint32_t base_pass_object_uniform_upload_count = 0u;
    frame_viewport.view_uniform_upload_count = &base_pass_view_uniform_upload_count;
    frame_viewport.object_uniform_upload_count = &base_pass_object_uniform_upload_count;
    frame_viewport.next_frame = make_frame();
    check(frame_manager.begin_init(base_pass_draw_resource).succeeded(),
          "Base Pass draw smoke must begin a pending resource transaction");
    toy3d::ForwardSceneRenderer base_pass_draw_renderer(make_base_pass_view_family());
    toy3d::SceneRenderTargets base_pass_draw_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> base_pass_draw_result =
        render_test_frame(base_pass_draw_renderer, *base_pass_scene, device, frame_manager, frame_viewport,
                          base_pass_draw_targets, tonemap_resources);
    const auto first_upload =
        std::find(base_pass_draw_operations.begin(), base_pass_draw_operations.end(), "upload_transient_uniform");
    const auto first_begin_pass =
        std::find(base_pass_draw_operations.begin(), base_pass_draw_operations.end(), "begin_render_pass");
    const auto draw_command =
        std::find(base_pass_draw_operations.begin(), base_pass_draw_operations.end(), "draw_indexed");
    const std::size_t draw_command_count = static_cast<std::size_t>(
        std::count(base_pass_draw_operations.begin(), base_pass_draw_operations.end(), "draw_indexed"));
    const auto first_end_pass = first_begin_pass == base_pass_draw_operations.end()
                                    ? base_pass_draw_operations.end()
                                    : std::find(first_begin_pass, base_pass_draw_operations.end(), "end_render_pass");
    check(base_pass_draw_result.succeeded() && first_upload != base_pass_draw_operations.end() &&
              first_begin_pass != base_pass_draw_operations.end() &&
              first_end_pass != base_pass_draw_operations.end() && first_upload < first_begin_pass &&
              std::find(first_begin_pass, first_end_pass, "device_create") == first_end_pass &&
              draw_command != base_pass_draw_operations.end() && draw_command_count == 4u &&
              base_pass_view_uniform_upload_count == 2u && base_pass_object_uniform_upload_count == 1u &&
              frame_viewport.end_count == 2u,
          "Base Pass must upload each View once and one shared Object for every draw of the same Primitive");
    const toy3d::RHIBindingSetRef first_base_pass_binding = view_infos(base_pass_draw_renderer)[0].view_binding();
    const toy3d::RHIBindingSetRef second_base_pass_binding = view_infos(base_pass_draw_renderer)[1].view_binding();
    decltype(context) shadow_style_context(device);
    const toy3d::RHIStatus shadow_style_status = toy3d::create_view_shader_bindings(
        device, shadow_style_context, view_infos(base_pass_draw_renderer));
    check(shadow_style_status.succeeded() && shadow_style_context.upload_count == 0u &&
              first_base_pass_binding == view_infos(base_pass_draw_renderer)[0].view_binding() &&
              second_base_pass_binding == view_infos(base_pass_draw_renderer)[1].view_binding(),
          "Shadow-style consumption must reuse the BasePass ViewInfo bindings without duplicate uploads");
    toy3d::RHIBindingSetRef frame_object_binding_ref =
        view_infos(base_pass_draw_renderer)[0].mesh_batches()[0].object_binding();
    bool all_draws_share_object_binding = frame_object_binding_ref != nullptr;
    for (const toy3d::ViewInfo& view_info : view_infos(base_pass_draw_renderer))
    {
        for (const toy3d::MeshBatch& mesh_batch : view_info.mesh_batches())
        {
            all_draws_share_object_binding =
                all_draws_share_object_binding && mesh_batch.object_binding() == frame_object_binding_ref;
        }
    }
    decltype(context) object_shadow_style_context(device);
    const toy3d::RHIStatus object_shadow_style_status = toy3d::create_object_shader_bindings(
        device, object_shadow_style_context, view_infos(base_pass_draw_renderer));
    check(object_shadow_style_status.succeeded() && object_shadow_style_context.upload_count == 0u &&
              all_draws_share_object_binding,
          "BasePass and Shadow-style consumers must share one frame-local Object upload and logical binding");
    std::weak_ptr<toy3d::RHIBindingSet> frame_object_binding = frame_object_binding_ref;
    view_infos(base_pass_draw_renderer).clear();
    frame_object_binding_ref.reset();
    check(frame_object_binding.expired(),
          "frame-local Object binding ownership must release when the frame's MeshBatch data is cleared");
    frame_viewport.view_uniform_upload_count = nullptr;
    frame_viewport.object_uniform_upload_count = nullptr;
    check(frame_manager.release(base_pass_draw_resource).succeeded(),
          "successful Base Pass draw resource must be releasable");

    const toy3d::ShaderMapProgramRef mixed_program =
        load_program(make_base_pass_program_with_required_material());
    toy3d::MaterialDesc missing_material_desc;
    missing_material_desc.shader_name = "Toy3d/Test/ViewObjectMaterial";
    missing_material_desc.parameter_schema = material_schema_from_program(*mixed_program);
    missing_material_desc.shader_program = mixed_program;
    missing_material_desc.scalar_defaults.emplace(11u, 0.25F);
    const toy3d::MaterialRef missing_material = toy3d::Material::create(std::move(missing_material_desc));
    toy3d::MaterialDesc complete_material_desc;
    complete_material_desc.shader_name = "Toy3d/Test/ViewObjectMaterial";
    complete_material_desc.parameter_schema = material_schema_from_program(*mixed_program);
    complete_material_desc.shader_program = mixed_program;
    complete_material_desc.scalar_defaults.emplace(11u, 0.5F);
    const toy3d::MaterialRef complete_material = toy3d::Material::create(std::move(complete_material_desc));
    const toy3d::MaterialInstanceRef missing_material_instance = toy3d::MaterialInstance::create(missing_material);
    const toy3d::MaterialInstanceRef complete_material_instance = toy3d::MaterialInstance::create(complete_material);

    toy3d::StaticMeshDesc mixed_mesh_desc;
    mixed_mesh_desc.vertices = {{{-0.5F, -0.5F, 4.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}},
                                {{0.5F, -0.5F, 4.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
                                {{0.0F, 0.5F, 4.0F}, {0.0F, 0.0F, 1.0F}, {0.5F, 1.0F}}};
    mixed_mesh_desc.indices = std::vector<std::uint16_t>{0u, 1u, 2u};
    mixed_mesh_desc.sections.push_back({0u, 3u, 0u});
    mixed_mesh_desc.sections.push_back({0u, 3u, 1u});
    mixed_mesh_desc.material_slots.push_back(missing_material_instance);
    mixed_mesh_desc.material_slots.push_back(complete_material_instance);
    const toy3d::StaticMeshRef mixed_mesh = toy3d::StaticMesh::create(std::move(mixed_mesh_desc));
    toy3d::StaticMeshRenderData mixed_render_data(*mixed_mesh);
    check(mixed_render_data.begin_init(frame_manager).succeeded() &&
              frame_manager.record_pending_uploads(context).succeeded() &&
              mixed_render_data.prepare_current_recording().succeeded() &&
              frame_manager.commit_recording().succeeded(),
          "mixed-validity Base Pass fixture must publish drawable mesh buffers");

    auto missing_material_proxy = std::make_unique<toy3d::MaterialRenderProxy>(*missing_material);
    auto complete_material_proxy = std::make_unique<toy3d::MaterialRenderProxy>(*complete_material);
    auto mixed_scene = std::make_unique<toy3d::RenderScene>(*material_graph, frame_manager);
    mixed_scene->add_primitive(std::make_unique<toy3d::StaticMeshSceneProxy>(
        toy3d::Matrix4::identity(), mixed_mesh->local_bounds(), true, &mixed_render_data,
        std::vector<toy3d::MaterialRenderProxy*>{missing_material_proxy.get(), complete_material_proxy.get()}));
    std::vector<toy3d::SceneView> mixed_views;
    mixed_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0F, 0.0F, 1.0F),
                             toy3d::IntRect{0, 0, 64u, 64u}, toy3d::Extent{64u, 64u},
                             toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0F), 0.1F, 100.0F);
    toy3d::ForwardSceneRenderer mixed_renderer(
        toy3d::SceneViewFamily(*mixed_scene, toy3d::Extent{64u, 64u}, std::move(mixed_views)));
    decltype(ready_first) mixed_resource;
    std::vector<std::string> mixed_operations;
    mixed_resource.operations = &mixed_operations;
    frame_viewport.operations = &mixed_operations;
    device.operations = &mixed_operations;
    frame_viewport.fail_next_material_uniform_upload = true;
    frame_viewport.next_frame = make_frame();
    frame_viewport.fail_next_material_uniform_upload = false;
    check(frame_manager.begin_init(mixed_resource).succeeded(),
          "mixed-validity Base Pass smoke must begin a pending resource transaction");
    toy3d::SceneRenderTargets mixed_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> mixed_result =
        render_test_frame(mixed_renderer, *mixed_scene, device, frame_manager, frame_viewport, mixed_targets,
                          tonemap_resources);
    const auto mixed_begin = std::find(mixed_operations.begin(), mixed_operations.end(), "begin_render_pass");
    const auto mixed_end = mixed_begin == mixed_operations.end()
                               ? mixed_operations.end()
                               : std::find(mixed_begin, mixed_operations.end(), "end_render_pass");
    const bool mixed_bindings_published = view_infos(mixed_renderer).size() == 1u &&
                                          view_infos(mixed_renderer)[0].mesh_batches().size() == 2u &&
                                          !view_infos(mixed_renderer)[0].mesh_batches()[0].material_binding() &&
                                          view_infos(mixed_renderer)[0].mesh_batches()[1].material_binding();
    check(mixed_result.succeeded() && mixed_bindings_published && mixed_begin != mixed_operations.end() &&
              mixed_end != mixed_operations.end() &&
              std::find(mixed_begin, mixed_end, "upload_transient_uniform") == mixed_end &&
              std::find(mixed_begin, mixed_end, "device_create") == mixed_end &&
              std::count(mixed_operations.begin(), mixed_operations.end(), "draw_indexed") == 1,
          "a missing required owner binding must skip only its batch while a later valid batch still records");
    check(frame_manager.release(mixed_resource).succeeded() && mixed_render_data.release(frame_manager).succeeded(),
          "mixed-validity Base Pass resources must release cleanly");
    mixed_scene.reset();
    missing_material_proxy.reset();
    complete_material_proxy.reset();

    decltype(ready_first) view_upload_failed_resource;
    std::vector<std::string> view_upload_failed_operations;
    view_upload_failed_resource.operations = &view_upload_failed_operations;
    frame_viewport.operations = &view_upload_failed_operations;
    device.operations = &view_upload_failed_operations;
    frame_viewport.next_frame = make_frame(false, true);
    const std::uint32_t abort_count_before_view_failure = frame_viewport.abort_count;
    check(frame_manager.begin_init(view_upload_failed_resource).succeeded(),
          "View upload failure smoke must begin a pending resource transaction");
    toy3d::ForwardSceneRenderer view_upload_failed_renderer(make_base_pass_view_family());
    toy3d::SceneRenderTargets view_upload_failed_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> view_upload_failed_result =
        render_test_frame(view_upload_failed_renderer, *base_pass_scene, device, frame_manager, frame_viewport,
                          view_upload_failed_targets, tonemap_resources);
    check(!view_upload_failed_result &&
              view_upload_failed_result.status().code() == toy3d::RHIErrorCode::Unsupported &&
              std::find(view_upload_failed_operations.begin(), view_upload_failed_operations.end(),
                        "begin_render_pass") == view_upload_failed_operations.end() &&
              view_upload_failed_resource.state() == toy3d::RenderResourceState::PendingUpload &&
              view_upload_failed_resource.discard_count == 1 &&
              frame_viewport.abort_count == abort_count_before_view_failure + 1u &&
              view_upload_failed_operations.back() == "abort_frame",
          "View upload failure must preserve its status, publish no binding, and abort before Base Pass begins");
    check(frame_manager.release(view_upload_failed_resource).succeeded(),
          "View upload failed resource must remain releasable");

    decltype(ready_first) draw_failed_resource;
    std::vector<std::string> draw_failed_operations;
    draw_failed_resource.operations = &draw_failed_operations;
    frame_viewport.operations = &draw_failed_operations;
    device.operations = &draw_failed_operations;
    frame_viewport.next_frame = make_frame(true);
    const std::uint32_t abort_count_before_draw_failure = frame_viewport.abort_count;
    check(frame_manager.begin_init(draw_failed_resource).succeeded(),
          "draw-failure smoke must begin a pending resource transaction");
    toy3d::ForwardSceneRenderer draw_failed_renderer(make_base_pass_view_family());
    toy3d::SceneRenderTargets draw_failed_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> draw_failed_result =
        render_test_frame(draw_failed_renderer, *base_pass_scene, device, frame_manager, frame_viewport,
                          draw_failed_targets, tonemap_resources);
    const auto failed_draw = std::find(draw_failed_operations.begin(), draw_failed_operations.end(), "draw_indexed");
    const auto end_after_failed_draw = failed_draw == draw_failed_operations.end()
                                           ? draw_failed_operations.end()
                                           : std::find(failed_draw, draw_failed_operations.end(), "end_render_pass");
    check(!draw_failed_result && draw_failed_result.status().code() == toy3d::RHIErrorCode::BackendFailure &&
              end_after_failed_draw != draw_failed_operations.end() &&
              draw_failed_resource.state() == toy3d::RenderResourceState::PendingUpload &&
              draw_failed_resource.discard_count == 1 &&
              frame_viewport.abort_count == abort_count_before_draw_failure + 1u &&
              draw_failed_operations.back() == "abort_frame",
          "a draw failure must preserve its error, end the pass, discard publication, and abort exactly once");
    check(frame_manager.release(draw_failed_resource).succeeded(),
          "draw-failed Base Pass resource must remain releasable");

    decltype(ready_first) invalid_pass_resource;
    std::vector<std::string> invalid_pass_operations;
    invalid_pass_resource.operations = &invalid_pass_operations;
    frame_viewport.operations = &invalid_pass_operations;
    device.operations = &invalid_pass_operations;
    frame_viewport.next_frame = make_frame();
    device.return_invalid_depth_view = true;
    const std::uint32_t abort_count_before_invalid_pass = frame_viewport.abort_count;
    check(frame_manager.begin_init(invalid_pass_resource).succeeded(),
          "invalid-pass smoke must begin a pending resource transaction");
    toy3d::ForwardSceneRenderer invalid_pass_renderer(make_base_pass_view_family());
    toy3d::SceneRenderTargets invalid_pass_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> invalid_pass_result =
        render_test_frame(invalid_pass_renderer, *base_pass_scene, device, frame_manager, frame_viewport,
                          invalid_pass_targets, tonemap_resources);
    device.return_invalid_depth_view = false;
    device.operations = nullptr;
    check(!invalid_pass_result && invalid_pass_result.status().code() == toy3d::RHIErrorCode::InvalidArgument &&
              std::find(invalid_pass_operations.begin(), invalid_pass_operations.end(), "begin_render_pass") ==
                  invalid_pass_operations.end() &&
              invalid_pass_resource.state() == toy3d::RenderResourceState::PendingUpload &&
              invalid_pass_resource.discard_count == 1 &&
              frame_viewport.abort_count == abort_count_before_invalid_pass + 1u &&
              invalid_pass_operations.back() == "abort_frame",
          "invalid prepare input must not begin a render pass and must discard and abort exactly once");
    check(frame_manager.release(invalid_pass_resource).succeeded(), "invalid-pass resource must remain releasable");
    check(base_pass_render_data.release(frame_manager).succeeded(),
          "Base Pass operation-order mesh resources must release cleanly");
    base_pass_scene.reset();
    base_pass_material_proxy.reset();

    decltype(ready_first) submit_failed_resource;
    std::vector<std::string> submit_failed_operations;
    submit_failed_resource.operations = &submit_failed_operations;
    frame_viewport.operations = &submit_failed_operations;
    frame_viewport.next_frame = make_frame();
    frame_viewport.end_success = false;
    check(frame_manager.begin_init(submit_failed_resource).succeeded(),
          "submit failure smoke must begin a retryable pending transaction");
    toy3d::ForwardSceneRenderer submit_failed_renderer(make_valid_view_family());
    toy3d::SceneRenderTargets submit_failed_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> submit_failed_result =
        render_test_frame(submit_failed_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          submit_failed_scene_render_targets, tonemap_resources);
    const std::vector<std::string> expected_submit_failed_operations = {"begin_frame",
                                                                        "begin_recording",
                                                                        "record_pending_uploads",
                                                                        "transition",
                                                                        "begin_render_pass",
                                                                        "end_render_pass",
                                                                        "transition",
                                                                        "transition",
                                                                        "begin_render_pass",
                                                                        "end_render_pass",
                                                                        "transition",
                                                                        "transition",
                                                                        "begin_render_pass",
                                                                        "end_render_pass",
                                                                        "transition",
                                                                        "upload_transient_uniform",
                                                                        "begin_render_pass",
                                                                        "set_graphics_pipeline",
                                                                        "draw",
                                                                        "end_render_pass",
                                                                        "transition",
                                                                        "finish_recording",
                                                                        "end_frame",
                                                                        "discard_recording"};
    check(!submit_failed_result && submit_failed_resource.state() == toy3d::RenderResourceState::PendingUpload &&
              submit_failed_resource.discard_count == 1 &&
              submit_failed_scene_render_targets.scene_color_access() == toy3d::RHIAccess::Common &&
              submit_failed_scene_render_targets.scene_depth_access() == toy3d::RHIAccess::Common &&
              submit_failed_operations == expected_submit_failed_operations,
          "submit failure must discard the recorded list transaction without publishing Ready");
    check(frame_manager.release(submit_failed_resource).succeeded(),
          "submit-failed resource must remain releasable after discard");
    frame_viewport.end_success = true;

    decltype(ready_first) suboptimal_present_resource;
    std::vector<std::string> suboptimal_present_operations;
    suboptimal_present_resource.operations = &suboptimal_present_operations;
    frame_viewport.operations = &suboptimal_present_operations;
    frame_viewport.next_frame = make_frame();
    frame_viewport.next_completion_value = 43u;
    frame_viewport.next_presentation_status =
        toy3d::RHIStatus::failure(toy3d::RHIErrorCode::Suboptimal, "Injected recoverable suboptimal presentation");
    check(frame_manager.begin_init(suboptimal_present_resource).succeeded(),
          "Suboptimal presentation smoke must begin a pending transaction");
    toy3d::ForwardSceneRenderer suboptimal_present_renderer(make_valid_view_family());
    toy3d::SceneRenderTargets suboptimal_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> suboptimal_present_result =
        render_test_frame(suboptimal_present_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          suboptimal_scene_render_targets, tonemap_resources);
    check(suboptimal_present_result.succeeded() && suboptimal_present_result.value().completion_value == 43u &&
              suboptimal_present_result.value().presentation_status.code() == toy3d::RHIErrorCode::Suboptimal &&
              suboptimal_present_resource.state() == toy3d::RenderResourceState::Ready &&
              suboptimal_present_operations == expected_submitted_operations,
          "Suboptimal after submit must preserve completion and commit resources");
    check(frame_manager.release(suboptimal_present_resource).succeeded(),
          "Suboptimal-presented resource must release after commit");

    decltype(ready_first) out_of_date_present_resource;
    std::vector<std::string> out_of_date_present_operations;
    out_of_date_present_resource.operations = &out_of_date_present_operations;
    frame_viewport.operations = &out_of_date_present_operations;
    frame_viewport.next_frame = make_frame();
    frame_viewport.next_completion_value = 44u;
    frame_viewport.next_presentation_status =
        toy3d::RHIStatus::failure(toy3d::RHIErrorCode::OutOfDate, "Injected recoverable out-of-date presentation");
    check(frame_manager.begin_init(out_of_date_present_resource).succeeded(),
          "OutOfDate presentation smoke must begin a pending transaction");
    toy3d::ForwardSceneRenderer out_of_date_present_renderer(make_valid_view_family());
    toy3d::SceneRenderTargets out_of_date_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> out_of_date_present_result =
        render_test_frame(out_of_date_present_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          out_of_date_scene_render_targets, tonemap_resources);
    check(out_of_date_present_result.succeeded() && out_of_date_present_result.value().completion_value == 44u &&
              out_of_date_present_result.value().presentation_status.code() == toy3d::RHIErrorCode::OutOfDate &&
              out_of_date_present_resource.state() == toy3d::RenderResourceState::Ready &&
              out_of_date_present_operations == expected_submitted_operations,
          "OutOfDate after submit must preserve completion and commit resources");
    check(frame_manager.release(out_of_date_present_resource).succeeded(),
          "OutOfDate-presented resource must release after commit");

    decltype(ready_first) unknown_boundary_resource;
    std::vector<std::string> unknown_boundary_operations;
    unknown_boundary_resource.operations = &unknown_boundary_operations;
    frame_viewport.operations = &unknown_boundary_operations;
    frame_viewport.next_frame = make_frame();
    frame_viewport.next_completion_value = 0u;
    frame_viewport.next_presentation_status = toy3d::RHIStatus::success();
    check(frame_manager.begin_init(unknown_boundary_resource).succeeded(),
          "unknown submit boundary smoke must begin a pending transaction");
    toy3d::ForwardSceneRenderer unknown_boundary_renderer(make_valid_view_family());
    toy3d::SceneRenderTargets unknown_boundary_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> unknown_boundary_result =
        render_test_frame(unknown_boundary_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          unknown_boundary_scene_render_targets, tonemap_resources);
    check(unknown_boundary_result.succeeded() && unknown_boundary_result.value().completion_value == 0u &&
              unknown_boundary_result.value().presentation_status.code() == toy3d::RHIErrorCode::BackendFailure &&
              unknown_boundary_resource.state() == toy3d::RenderResourceState::Ready &&
              unknown_boundary_operations == expected_submitted_operations,
          "unknown completion metadata after submit must surface terminal presentation status without rolling back "
          "resources");
    check(frame_manager.release(unknown_boundary_resource).succeeded(),
          "unknown-boundary resource must release after commit");
    frame_viewport.next_completion_value = 42u;
    frame_viewport.next_presentation_status = toy3d::RHIStatus::success();

    decltype(ready_first) resize_race_resource;
    std::vector<std::string> resize_race_operations;
    resize_race_resource.operations = &resize_race_operations;
    frame_viewport.operations = &resize_race_operations;
    frame_viewport.next_frame = make_frame();
    const std::uint32_t abort_count_before_resize_race = frame_viewport.abort_count;
    check(frame_manager.begin_init(resize_race_resource).succeeded(),
          "resize-race smoke must begin a retryable pending resource transaction");
    std::vector<toy3d::SceneView> resized_views;
    resized_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                               toy3d::IntRect{0, 0, 128u, 64u}, toy3d::Extent{128u, 64u},
                               toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 100.0f);
    toy3d::ForwardSceneRenderer resize_race_renderer(
        toy3d::SceneViewFamily(*frame_render_scene, toy3d::Extent{128u, 64u}, std::move(resized_views)));
    toy3d::SceneRenderTargets resize_race_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> resize_race_result =
        render_test_frame(resize_race_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          resize_race_scene_render_targets, tonemap_resources);
    const std::vector<std::string> expected_resize_race_operations = {"begin_frame", "abort_frame"};
    check(!resize_race_result && resize_race_result.status().code() == toy3d::RHIErrorCode::OutOfDate &&
              resize_race_resource.state() == toy3d::RenderResourceState::PendingUpload &&
              resize_race_resource.discard_count == 0 && resize_race_operations == expected_resize_race_operations &&
              frame_viewport.abort_count == abort_count_before_resize_race + 1u,
          "a resize extent race must abort acquired work and remain recoverable");
    check(frame_manager.release(resize_race_resource).succeeded(),
          "resize-race resource must remain releasable after abort");

    decltype(ready_first) aborted_frame_resource;
    std::vector<std::string> aborted_frame_operations;
    aborted_frame_resource.operations = &aborted_frame_operations;
    frame_viewport.operations = &aborted_frame_operations;
    frame_viewport.next_frame = make_frame();
    const std::uint32_t abort_count_before_invalid_views = frame_viewport.abort_count;
    check(frame_manager.begin_init(aborted_frame_resource).succeeded(),
          "abort smoke must begin a retryable pending resource transaction");
    std::vector<toy3d::SceneView> invalid_views;
    invalid_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                               toy3d::IntRect{0, 0, 0u, 0u}, toy3d::Extent{64u, 64u},
                               toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 100.0f);
    toy3d::ForwardSceneRenderer aborted_frame_renderer(
        toy3d::SceneViewFamily(*frame_render_scene, toy3d::Extent{64u, 64u}, std::move(invalid_views)));
    toy3d::SceneRenderTargets aborted_scene_render_targets;
    const toy3d::RHIResult<toy3d::RHIFrameEndResult> aborted_frame_result =
        render_test_frame(aborted_frame_renderer, *frame_render_scene, device, frame_manager, frame_viewport,
                          aborted_scene_render_targets, tonemap_resources);
    const std::vector<std::string> expected_aborted_operations = {
        "begin_frame", "begin_recording", "record_pending_uploads", "discard_recording", "abort_frame"};
    check(!aborted_frame_result && aborted_frame_resource.state() == toy3d::RenderResourceState::PendingUpload &&
              aborted_frame_resource.discard_count == 1 && aborted_frame_operations == expected_aborted_operations &&
              frame_viewport.abort_count == abort_count_before_invalid_views + 1u,
          "invalid init_views after acquire must discard resource publication and abort exactly once");
    check(frame_manager.release(aborted_frame_resource).succeeded(),
          "aborted frame resource must remain releasable after discard");
    frame_render_scene.reset();

    toy3d::ShaderMapProgramData active_program_data = make_material_program("Main", 20u);
    active_program_data.graphics_pass_state.cull_mode = toy3d::shader::ShaderGraphicsPassState::CullMode::Front;
    active_program_data.pass_template_hash =
        toy3d::shader::calculate_shader_graphics_pass_state_hash(active_program_data.graphics_pass_state);
    toy3d::ShaderMapProgramData mapping_only_program_data = active_program_data;
    mapping_only_program_data.bindings[0u].target_binding = 2u;
    mapping_only_program_data.bindings[1u].target_binding = 3u;
    mapping_only_program_data.stages.front().reflection = mapping_only_program_data.bindings;
    mapping_only_program_data.target_binding_hash = nonzero_hash(90u);
    const toy3d::ShaderMapProgramRef active_program = load_program(std::move(active_program_data));
    const toy3d::ShaderMapProgramRef mapping_only_program = load_program(std::move(mapping_only_program_data));
    const toy3d::ShaderMapProgramRef candidate_program = load_program(make_material_program("Candidate", 40u));
    toy3d::ShaderMapProgramData subset_program_data = make_material_program("TextureInactive", 50u);
    subset_program_data.bindings.erase(
        std::remove_if(subset_program_data.bindings.begin(), subset_program_data.bindings.end(),
                       [](const toy3d::ShaderMapBinding& binding)
                       { return binding.type == toy3d::RHIResourceBindingType::SampledTexture; }),
        subset_program_data.bindings.end());
    subset_program_data.stages.front().reflection = subset_program_data.bindings;
    const toy3d::ShaderMapProgramRef subset_program = load_program(std::move(subset_program_data));
    toy3d::ShaderMapProgramData incompatible_schema_program_data = make_material_program("IncompleteCandidate", 60u);
    incompatible_schema_program_data.bindings.front().constant_members.erase(
        incompatible_schema_program_data.bindings.front().constant_members.begin());
    incompatible_schema_program_data.stages.front().reflection = incompatible_schema_program_data.bindings;
    toy3d::tests::finalize_test_program_parameter_schema(incompatible_schema_program_data);
    const toy3d::ShaderMapProgramRef incompatible_schema_program =
        load_program(std::move(incompatible_schema_program_data));
    toy3d::MaterialDesc render_material_desc;
    render_material_desc.shader_name = "Toy3d/Test/Material";
    render_material_desc.parameter_schema = material_schema_from_program(*active_program);
    render_material_desc.shader_program = active_program;
    render_material_desc.scalar_defaults.emplace(11u, 0.25f);
    render_material_desc.vector4_defaults.emplace(13u, toy3d::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    render_material_desc.texture_defaults.emplace(12u, texture);
    const toy3d::MaterialRef render_material = toy3d::Material::create(std::move(render_material_desc));
    toy3d::MaterialDesc subset_material_desc;
    subset_material_desc.shader_name = "Toy3d/Test/Material";
    subset_material_desc.parameter_schema = material_schema_from_program(*active_program);
    subset_material_desc.shader_program = subset_program;
    subset_material_desc.scalar_defaults.emplace(11u, 0.25f);
    subset_material_desc.vector4_defaults.emplace(13u, toy3d::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    subset_material_desc.texture_defaults.emplace(12u, texture);
    const toy3d::MaterialRef subset_material = toy3d::Material::create(std::move(subset_material_desc));
    check(render_material != nullptr && subset_material != nullptr &&
              render_material->parameter_schema().schema_identity ==
                  subset_material->parameter_schema().schema_identity &&
              subset_material->parameter_schema().constant_buffers.size() == 1u &&
              subset_material->parameter_schema().constant_buffers.front().members.size() == 2u &&
              subset_material->parameter_schema().constant_buffers.front().members.front().name == "roughness" &&
              subset_material->parameter_schema().constant_buffers.front().members.front().parameter_id == 11u &&
              subset_material->parameter_schema().constant_buffers.front().members.front().type ==
                  toy3d::shader::ShaderValueType::Float32 &&
              subset_material->parameter_schema().constant_buffers.front().members.front().offset == 0u &&
              subset_material->parameter_schema().constant_buffers.front().members.front().size == 4u &&
              subset_material->parameter_schema().constant_buffers.front().members.front().default_value.size() ==
                  4u &&
              subset_material->parameter_schema().resources.size() == 1u &&
              subset_material->parameter_schema().resources.front().name == "base_color_texture" &&
              subset_material->parameter_schema().resources.front().parameter_id == 12u &&
              subset_material->parameter_schema().resources.front().category ==
                  toy3d::shader::ShaderParameterCategory::SampledTexture &&
              subset_material->parameter_schema().resources.front().resource_kind ==
                  toy3d::shader::ResourceKind::Texture2D &&
              subset_material->parameter_schema().resources.front().default_value == "Builtin/White" &&
              subset_program->data().bindings.size() == 1u,
          "Material variants with different active resource subsets must share one complete runtime schema");

    {
        const auto asset_program = load_program(make_material_program("Forward", 110u));
        toy3d::MaterialTextureValues resolved;
        resolved.named_defaults.emplace("Builtin/White", texture);
        toy3d::MaterialAssetData asset_data;
        asset_data.shader_name = "Toy3d/Test/Material";
        asset_data.overrides = {{"roughness", 0.75f}, {"base_color", toy3d::Vector4(0.2f, 0.3f, 0.4f, 1.0f)}};
        toy3d::MaterialInstanceRef root_instance;
        {
            const auto built = toy3d::create_material_from_asset(asset_data, asset_program, resolved);
            check(built.succeeded(), "asset material must build from compiled schema and resolved textures");
            root_instance = built.value();
        }
        const auto root_binding = root_instance->material_render_proxy()->materialize(device, context);
        float root_scalar = 0;
        check(root_binding.succeeded() && context.last_buffer_upload_data.size() == 32u, "root material must upload a complete constant buffer");
        std::memcpy(&root_scalar, context.last_buffer_upload_data.data(), sizeof(root_scalar));
        check(root_binding.succeeded() && root_scalar == 0.75f &&
            root_instance->material()->desc().scalar_defaults.at(11u) == 0.25f,
            "root dynamic overrides must not overwrite immutable Shader defaults");
        toy3d::MaterialInstanceAssetData child_data;
        check(toy3d::AssetId::parse("11111111111111111111111111111111", child_data.parent.asset_id), "parent fixture ID failed");
        child_data.parent.expected_type = "toy3d.MaterialAssetData";
        child_data.overrides = {{"roughness", 0.9f}};
        toy3d::MaterialInstanceRef child_instance;
        {
            const auto built = toy3d::create_material_instance_from_asset(child_data, root_instance, resolved);
            check(built.succeeded(), "child material must combine parent and child overrides");
            child_instance = built.value();
        }
        const auto child_binding = child_instance->material_render_proxy()->materialize(device, context);
        float child_scalar = 0;
        float inherited_color = 0;
        check(child_binding.succeeded() && context.last_buffer_upload_data.size() == 32u, "child material must upload a complete constant buffer");
        std::memcpy(&child_scalar, context.last_buffer_upload_data.data(), sizeof(child_scalar));
        std::memcpy(&inherited_color, context.last_buffer_upload_data.data() + 16u, sizeof(inherited_color));
        check(child_binding.succeeded() && child_scalar == 0.9f && inherited_color == 0.2f &&
            child_instance->material() == root_instance->material(), "child must inherit root values and share immutable Material");
        child_data.overrides.clear();
        toy3d::MaterialInstanceRef inherited_instance;
        {
            const auto built = toy3d::create_material_instance_from_asset(child_data, root_instance, resolved);
            check(built.succeeded(), "empty child override set must inherit parent");
            inherited_instance = built.value();
        }
        check(inherited_instance->material_render_proxy()->materialize(device, context).succeeded(), "inherited instance materialization failed");
        check(context.last_buffer_upload_data.size() == 32u, "inherited material must upload a complete constant buffer");
        std::memcpy(&child_scalar, context.last_buffer_upload_data.data(), sizeof(child_scalar));
        check(child_scalar == 0.75f, "removing a child override must use parent value");
        auto invalid_data = asset_data;
        invalid_data.overrides.push_back(invalid_data.overrides.front());
        check(!toy3d::create_material_from_asset(invalid_data, asset_program, resolved).succeeded(), "duplicate override must reject full candidate");
        check(!toy3d::create_material_from_asset(asset_data, asset_program, {}).succeeded(), "unresolved default texture must fail");
        invalid_data = asset_data;
        invalid_data.overrides.push_back({"removed_parameter", 1.0f});
        toy3d::MaterialInstanceRef orphan_instance;
        {
            const auto built = toy3d::create_material_from_asset(invalid_data, asset_program, resolved);
            check(built.succeeded(), "known orphan must not stop valid runtime material construction");
            orphan_instance = built.value();
        }
        const auto stable_proxy = root_instance->material_render_proxy();
        const toy3d::MaterialParameterChanges batch = {
            {"roughness", 0.6f}, {"base_color", toy3d::Vector4(0.3f, 0.4f, 0.5f, 1.0f)}};
        check(root_instance->apply_parameters(batch), "complete dynamic batch must apply");
        const auto batched_binding = stable_proxy->materialize(device, context);
        float batch_scalar = 0.0f;
        float batch_color = 0.0f;
        if (context.last_buffer_upload_data.size() == 32u)
        {
            std::memcpy(&batch_scalar, context.last_buffer_upload_data.data(), sizeof(float));
            std::memcpy(&batch_color, context.last_buffer_upload_data.data() + 16u, sizeof(float));
        }
        check(batched_binding.succeeded() && batch_scalar == 0.6f && batch_color == 0.3f &&
            root_instance->material_render_proxy() == stable_proxy, "batch preserves proxy and publishes every parameter");
        const auto inherited_binding = inherited_instance->material_render_proxy()->materialize(device, context);
        std::memcpy(&child_scalar, context.last_buffer_upload_data.data(), sizeof(child_scalar));
        std::memcpy(&inherited_color, context.last_buffer_upload_data.data() + 16u, sizeof(inherited_color));
        check(inherited_binding.succeeded() && child_scalar == 0.6f && inherited_color == 0.3f,
            "parent updates reach the actual descendant GPU constant binding");
        auto grand_instance = toy3d::MaterialInstance::create(child_instance);
        check(grand_instance && grand_instance->parent() == child_instance && !grand_instance->overrides_parameter("roughness"),
            "runtime third layer retains direct Parent and empty local overrides");
        check(child_instance->set_scalar("roughness", 0.82f) && grand_instance->set_scalar("roughness", 0.95f) &&
            grand_instance->reset_parameter("roughness"), "third-layer reset restores latest Parent");
        const auto grand_binding = grand_instance->material_render_proxy()->materialize(device, context);
        std::memcpy(&child_scalar, context.last_buffer_upload_data.data(), sizeof(child_scalar));
        check(grand_binding.succeeded() && child_scalar == 0.82f, "third-layer GPU values use live Parent instead of copied defaults");
        toy3d::MaterialInstance::release(grand_instance);
        check(!root_instance->apply_parameters({{"roughness", 0.7f}, {"unknown", 1.0f}}) &&
            !root_instance->apply_parameters({{"roughness", 0.7f}, {"roughness", 0.8f}}) &&
            !root_instance->set_scalar("roughness", std::numeric_limits<float>::quiet_NaN()) &&
            stable_proxy->materialize(device, context).value() == batched_binding.value(),
            "unknown duplicate or non-finite batch must not publish a partial update or dirty bindings");
        check(root_instance->reset_parameter("roughness") && root_instance->reset_parameter("base_color"),
            "runtime reset must resolve immutable Shader defaults");
        const auto reset_binding = stable_proxy->materialize(device, context);
        if (context.last_buffer_upload_data.size() == 32u)
            std::memcpy(&batch_scalar, context.last_buffer_upload_data.data(), sizeof(float));
        check(reset_binding.succeeded() && batch_scalar == 0.25f, "runtime reset uses Shader default rather than root override");
        toy3d::MaterialInstance::release(orphan_instance);
        toy3d::MaterialInstance::release(inherited_instance);
        toy3d::MaterialInstance::release(child_instance);
        toy3d::MaterialInstance::release(root_instance);
    }

    const std::uint32_t invisible_buffer_count = device.buffer_creation_count;
    toy3d::MaterialInstanceRef invisible_material_instance = toy3d::MaterialInstance::create(render_material);
    check(invisible_material_instance->set_scalar("roughness", 0.375f),
          "an invisible MaterialInstance must accept a schema-validated dirty update");
    toy3d::MaterialInstance::release(invisible_material_instance);
    check(device.buffer_creation_count == invisible_buffer_count,
          "an invisible dirty MaterialInstance must not allocate constants or bindings before a visible draw");

    toy3d::MaterialInstanceRef subset_material_instance = toy3d::MaterialInstance::create(subset_material);
    toy3d::RHIResult<toy3d::RHIBindingSetRef> subset_material_binding =
        subset_material_instance->material_render_proxy()->materialize(device, context);
    bool subset_has_constants = false;
    bool subset_has_inactive_texture = false;
    if (subset_material_binding)
    {
        for (const toy3d::RHIBindingValue& value : subset_material_binding.value()->desc().bindings)
        {
            subset_has_constants = subset_has_constants || value.binding_id == 10u;
            subset_has_inactive_texture = subset_has_inactive_texture || value.binding_id == 12u;
        }
    }
    check(subset_material_binding.succeeded() &&
              subset_material_binding.value()->desc().bindings.size() == 2u && subset_has_constants &&
              subset_has_inactive_texture,
          "Material logical materialization must include the complete schema superset even when the Program "
          "active layout omits a Texture");
    toy3d::MaterialInstance::release(subset_material_instance);

    toy3d::MaterialInstanceRef render_material_instance = toy3d::MaterialInstance::create(render_material);
    check(render_material_instance != nullptr &&
              render_material_instance->set_scalar("roughness", 0.5f) &&
              render_material_instance->set_texture("base_color_texture", texture) &&
              !render_material_instance->set_vector("roughness", toy3d::vec4(1.0f, 0.0f, 0.0f, 1.0f)) &&
              !render_material_instance->set_scalar("base_color_texture", 1.0f) &&
              !render_material_instance->set_scalar("unknown_parameter", 1.0f) &&
              !render_material_instance->set_texture("roughness", texture) &&
              !render_material_instance->set_texture("base_color_texture", nullptr),
          "a setter with the wrong reflected type must fail without mutation");

    toy3d::MaterialRenderProxy* const material_proxy = render_material_instance->material_render_proxy();
    const std::uint32_t buffer_count_before_first_draw = device.buffer_creation_count;
    toy3d::RHIResult<toy3d::RHIBindingSetRef> first_material_binding =
        material_proxy->materialize(device, context);
    float materialized_scalar = 0.0f;
    if (context.last_buffer_upload_data.size() >= sizeof(float))
    {
        std::memcpy(&materialized_scalar, context.last_buffer_upload_data.data(), sizeof(float));
    }
    check(first_material_binding.succeeded() && materialized_scalar == 0.5f,
          "invalid name and type requests must leave the last valid GT/RT scalar update intact");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> binding_after_failed_updates =
        material_proxy->materialize(device, context);
    check(binding_after_failed_updates.succeeded() &&
              binding_after_failed_updates.value() == first_material_binding.value() &&
              device.buffer_creation_count == buffer_count_before_first_draw + 1u,
          "same-frame draws must reuse one Material logical binding and failed setters must not dirty it");

    // Mutating this caller-owned string immediately after both calls proves the
    // queued commands retain only the resolved parameter ID and owned scalar values.
    std::string canonical_parameter_name = "roughness";
    check(render_material_instance->set_scalar(canonical_parameter_name, 0.625f) &&
              render_material_instance->set_scalar(canonical_parameter_name, 0.75f),
          "valid Material setters must enqueue their FIFO proxy updates");
    canonical_parameter_name = "base_color_texture";
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> fifo_material_binding =
        material_proxy->materialize(device, context);
    materialized_scalar = 0.0f;
    if (context.last_buffer_upload_data.size() >= sizeof(float))
    {
        std::memcpy(&materialized_scalar, context.last_buffer_upload_data.data(), sizeof(float));
    }
    check(fifo_material_binding.succeeded() && materialized_scalar == 0.75f,
          "RT must apply consecutive resolved-ID updates in FIFO order without retaining the caller's name");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> unchanged_material_binding =
        material_proxy->materialize(device, context);
    check(unchanged_material_binding.succeeded() &&
              unchanged_material_binding.value() == fifo_material_binding.value(),
          "unchanged Material parameters and Texture binding identity must reuse the binding");
    check(render_material_instance->set_scalar("roughness", 0.75f),
          "a repeated Material value update must remain a valid GT operation");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> same_value_material_binding =
        material_proxy->materialize(device, context);
    check(same_value_material_binding.succeeded() &&
              same_value_material_binding.value() == unchanged_material_binding.value(),
          "an equal scalar value must not invalidate the Material logical binding");

    const toy3d::RHITextureViewRef first_view = texture_resource->active_view();
    toy3d::TextureDesc content_update = texture->desc();
    content_update.mip_pixels[0].assign(64u, 127u);
    check(texture_resource->update(std::move(content_update), manager) && manager.record_pending_uploads(context) &&
              texture_resource->view_for_current_recording() == first_view && manager.commit_recording() &&
              texture_resource->active_view() == first_view && texture_resource->binding_generation() == 1u,
          "same-layout Texture content update must keep view identity and binding generation");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> content_updated_material_binding =
        material_proxy->materialize(device, context);
    check(content_updated_material_binding.succeeded() &&
              content_updated_material_binding.value() == unchanged_material_binding.value(),
          "same-view Texture content updates must not rebuild Material bindings");

    toy3d::TextureDesc replacement_desc;
    replacement_desc.width = 8;
    replacement_desc.height = 4;
    replacement_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    replacement_desc.row_pitches = {32u};
    replacement_desc.slice_pitches = {128u};
    replacement_desc.mip_pixels = {std::vector<std::uint8_t>(128u, 63u)};
    check(texture_resource->update(std::move(replacement_desc), manager) && manager.record_pending_uploads(context) &&
              texture_resource->view_for_current_recording() != first_view &&
              texture_resource->active_view() == first_view && texture_resource->binding_generation() == 1u,
          "Texture replacement candidate must be current-list eligible without replacing active before submit");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> replacement_material_binding =
        material_proxy->materialize(device, context);
    check(replacement_material_binding.succeeded() &&
              replacement_material_binding.value() != content_updated_material_binding.value(),
          "a replacement candidate view must invalidate the current-list Material binding");
    check(manager.commit_recording() && texture_resource->active_view() != first_view &&
              texture_resource->binding_generation() == 2u && first_view != nullptr,
          "Texture replacement commit must increment generation while prior strong view references remain valid");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> committed_material_binding =
        material_proxy->materialize(device, context);
    check(committed_material_binding.succeeded() &&
              committed_material_binding.value() != replacement_material_binding.value(),
          "a committed Texture binding generation change must invalidate cached Material bindings");

    const toy3d::RHIResult<toy3d::RHIBindingSetRef> active_candidate_binding =
        material_proxy->materialize(device, context);
    check(active_candidate_binding.succeeded() && material_proxy->effective_graphics_pass_state() != nullptr &&
              material_proxy->effective_graphics_pass_state()->cull_mode ==
                  toy3d::shader::ShaderGraphicsPassState::CullMode::Front,
          "a single-sided active candidate must preserve the Shader Pass cull mode");

    const toy3d::RHIStatus direct_incomplete_stage = material_proxy->stage_material_candidate(candidate_program, true);
    const toy3d::RHIStatus direct_incomplete_commit = material_proxy->commit_material_candidate();
    const toy3d::RHIStatus direct_retry_stage = material_proxy->stage_material_candidate(candidate_program, true);
    check(direct_incomplete_stage && !direct_incomplete_commit && direct_retry_stage,
          "a failed direct candidate commit must discard staged state and allow retry");
    material_proxy->discard_material_candidate();
    check(!material_proxy->stage_material_candidate(incompatible_schema_program, true),
          "the Render-side Material schema must reject a candidate with a different complete identity");

    const bool incomplete_candidate_staged =
        render_material_instance->stage_material_replacement(incompatible_schema_program, true);
    const bool incomplete_candidate_published = render_material_instance->publish_material_replacement();
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> active_binding_after_failed_candidate =
        material_proxy->materialize(device, context);
    const bool old_schema_scalar_accepted = render_material_instance->set_scalar("roughness", 0.75f);
    check(!incomplete_candidate_staged && !incomplete_candidate_published &&
              material_proxy->shader_program() == active_program &&
              material_proxy->effective_graphics_pass_state() != nullptr &&
              material_proxy->effective_graphics_pass_state()->cull_mode ==
                  toy3d::shader::ShaderGraphicsPassState::CullMode::Front &&
              active_binding_after_failed_candidate.succeeded() &&
              active_binding_after_failed_candidate.value() == active_candidate_binding.value() &&
              old_schema_scalar_accepted,
          "a candidate with a different complete Material schema must be rejected before publication");

    const toy3d::RHIBindingSetRef binding_before_mapping_candidate = active_binding_after_failed_candidate.value();
    check(active_program->data().target_binding_hash != mapping_only_program->data().target_binding_hash &&
              active_program->data().bindings[0u].target_binding !=
                  mapping_only_program->data().bindings[0u].target_binding &&
              render_material_instance->stage_material_replacement(mapping_only_program, false),
          "a schema-compatible Program mapping candidate must stage successfully");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> mapping_candidate_binding =
        material_proxy->materialize_staged(device, context);
    check(mapping_candidate_binding.succeeded() &&
              mapping_candidate_binding.value() == binding_before_mapping_candidate &&
              render_material_instance->publish_material_replacement() &&
              material_proxy->shader_program() == mapping_only_program &&
              material_proxy->materialize(device, context).value() == binding_before_mapping_candidate,
          "a Program target-mapping change must publish while reusing the Program-independent Material binding");
    check(render_material_instance->stage_material_replacement(active_program, false) &&
              material_proxy->materialize_staged(device, context).value() == binding_before_mapping_candidate &&
              render_material_instance->publish_material_replacement(),
          "restoring the original Program mapping must also reuse the Material logical binding");

    check(render_material_instance->stage_material_replacement(subset_program, false),
          "a Program candidate with a smaller active Material subset must stage successfully");
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> subset_candidate_binding =
        material_proxy->materialize_staged(device, context);
    check(subset_candidate_binding.succeeded() && subset_candidate_binding.value() == binding_before_mapping_candidate &&
              render_material_instance->publish_material_replacement() &&
              material_proxy->shader_program() == subset_program &&
              material_proxy->materialize(device, context).value() == binding_before_mapping_candidate,
          "an active-subset-only Program change must reuse the complete Material logical superset");
    check(render_material_instance->stage_material_replacement(active_program, false) &&
              material_proxy->materialize_staged(device, context).value() == binding_before_mapping_candidate &&
              render_material_instance->publish_material_replacement(),
          "restoring the full active subset must not rebuild the unchanged Material logical superset");

    check(render_material_instance->stage_material_replacement(candidate_program, true) &&
              material_proxy->materialize_staged(device, context).succeeded() &&
              render_material_instance->discard_material_replacement() &&
              material_proxy->shader_program() == active_program &&
              material_proxy->effective_graphics_pass_state() != nullptr &&
              material_proxy->effective_graphics_pass_state()->cull_mode ==
                  toy3d::shader::ShaderGraphicsPassState::CullMode::Front,
          "discarding a fully materialized candidate must preserve active Program and state");

    const toy3d::RHIResult<toy3d::RHIBindingSetRef> active_binding_before_replacement =
        material_proxy->materialize(device, context);
    const bool staged_two_sided = render_material_instance->stage_material_replacement(candidate_program, true);
    toy3d::RHIResult<toy3d::RHIBindingSetRef> staged_two_sided_binding =
        material_proxy->materialize_staged(device, context);
    const toy3d::RHIResult<toy3d::RHIBindingSetRef> active_binding_while_candidate_staged =
        material_proxy->materialize(device, context);
    check(staged_two_sided && staged_two_sided_binding.succeeded() &&
              active_binding_before_replacement.succeeded() &&
              active_binding_while_candidate_staged.succeeded() &&
              active_binding_while_candidate_staged.value() == active_binding_before_replacement.value() &&
              staged_two_sided_binding.value() == active_binding_before_replacement.value() &&
              material_proxy->shader_program() == active_program &&
              render_material_instance->publish_material_replacement() &&
              material_proxy->shader_program() == candidate_program &&
              material_proxy->effective_graphics_pass_state() != nullptr &&
              material_proxy->effective_graphics_pass_state()->cull_mode ==
                  toy3d::shader::ShaderGraphicsPassState::CullMode::None &&
              material_proxy->materialize(device, context).value() == staged_two_sided_binding.value(),
          "two-sided publication must atomically commit Program, CullMode::None, and binding");

    check(render_material_instance->stage_material_replacement(candidate_program, false) &&
              material_proxy->materialize_staged(device, context).succeeded() &&
              render_material_instance->publish_material_replacement() &&
              material_proxy->effective_graphics_pass_state() != nullptr &&
              material_proxy->effective_graphics_pass_state()->cull_mode ==
                  candidate_program->data().graphics_pass_state.cull_mode,
          "disabling two-sided must restore the Shader Pass cull mode in the next candidate");
    toy3d::MaterialInstance::release(render_material_instance);
    check(!render_material_instance && material_rendering_thread.stop().succeeded(),
          "Material proxy release must execute before Rendering Thread teardown");
    check(material_graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
          "Material fixture Task Graph must shut down cleanly");
    material_graph.reset();

    check(texture_resource->release(manager).succeeded(),
          "TextureResource release must detach manager state without waiting for GPU completion");

    deterministic.deterministic_failure = true;
    check(manager.begin_init(deterministic).succeeded(), "deterministic failure resource must begin as PendingUpload");
    const toy3d::RHIStatus deterministic_status = manager.record_pending_uploads(context);
    check(!deterministic_status && deterministic.state() == toy3d::RenderResourceState::Failed &&
              deterministic.failure_status().code() == toy3d::RHIErrorCode::Unsupported,
          "resource-local Unsupported must publish Failed with the original diagnostic");
    check(manager.discard_recording() && manager.release(deterministic),
          "Failed resources must be removable and releasable");

    check(manager.begin_init(released_while_recording) && manager.record_pending_uploads(context),
          "pending release smoke must first enter the current recording collection");
    check(manager.release(released_while_recording) &&
              released_while_recording.state() == toy3d::RenderResourceState::Released &&
              released_while_recording.discard_count == 1 && released_while_recording.release_count == 1,
          "release before submit must discard Ready publication and release only CPU refs");
    check(manager.commit_recording().succeeded(),
          "commit after a removed recording entry must not republish the released resource");

    toy3d::RenderResourceManager terminal_manager(device);
    check(terminal_manager.begin_init(terminal_first) && terminal_manager.begin_init(terminal_second) &&
              terminal_manager.record_pending_uploads(context),
          "terminal smoke must establish pending and recording non-owning entries");
    check(terminal_manager.clear_for_terminal() && terminal_first.discard_count == 1 &&
              terminal_second.discard_count == 1,
          "terminal clear must discard recording and detach every pending pointer first");
    check(!terminal_manager.record_pending_uploads(context),
          "terminal-cleared manager must never dereference resources again");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "Render resource manager tests passed\n";
    return 0;
}
