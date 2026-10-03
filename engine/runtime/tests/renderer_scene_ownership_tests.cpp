#include "rendercore/scene_interface.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/scene/light_scene_proxy.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/view/scene_view.h"
#include "shader_map_test_utils.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"
#include "rendercore/frame_synchronization.h"
#include "rendercore/render_command_internal.h"
#include "rendercore/rendering_thread.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"
#include "gamescene/world/world.h"
#include "renderscene/primitive_scene_info.h"
#include "rendercore/render_resource_manager.h"
#include "renderscene/render_scene.h"
#include "renderscene/renderer.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/ui/imgui_renderer.h"
#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_queue.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "renderscene/view/forward_scene_renderer.h"
#include "renderscene/view/scene_visibility.h"
#include "renderscene/pass/shadow_pass.h"
#include "renderscene/view/view_shader_bindings.h"

namespace
{
    toy3d::ShaderContentHash nonzero_hash(std::uint8_t value)
    {
        toy3d::ShaderContentHash hash{};
        hash[0] = value;
        return hash;
    }

    void finalize_generated_global_program(toy3d::ShaderMapProgramData& program,
                                           const toy3d::ShaderParametersMetadata& metadata)
    {
        toy3d::tests::append_shader_parameters_metadata(metadata, program.parameter_schema);
        const toy3d::ViewShaderParameters view_parameters;
        const toy3d::ObjectShaderParameters object_parameters;
        toy3d::tests::append_shader_parameters_metadata(toy3d::shader_parameters_metadata(view_parameters),
                                                        program.parameter_schema);
        toy3d::tests::append_shader_parameters_metadata(toy3d::shader_parameters_metadata(object_parameters),
                                                        program.parameter_schema);
        std::sort(program.parameter_schema.constant_buffers.begin(), program.parameter_schema.constant_buffers.end(),
                  [](const toy3d::shader::ShaderParameterConstantBufferSchema& left,
                     const toy3d::shader::ShaderParameterConstantBufferSchema& right)
                  {
                      return left.group < right.group;
                  });
        program.parameter_schema.logical_layout_hash =
            toy3d::shader::calculate_shader_parameter_logical_layout_hash(program.parameter_schema);
        program.parameter_schema.schema_identity =
            toy3d::shader::calculate_shader_parameter_schema_identity(program.parameter_schema);
        program.logical_layout_hash = program.parameter_schema.logical_layout_hash;
    }

    class RendererProgramLoader final : public toy3d::ShaderMapLoader
    {
      public:
        explicit RendererProgramLoader(toy3d::ShaderMapProgramData program) : programs_{std::move(program)}
        {
        }

        explicit RendererProgramLoader(std::vector<toy3d::ShaderMapProgramData> programs)
            : programs_(std::move(programs))
        {
        }

        toy3d::ShaderMapProgramLoadResult load_program(const toy3d::ShaderMapProgramKey& key) const override
        {
            for (const toy3d::ShaderMapProgramData& program : programs_)
            {
                if (program.shader_name == key.shader_name && program.pass_name == key.pass_name &&
                    program.platform == key.platform && program.permutation_key == key.permutation_key)
                {
                    return {program, {}};
                }
            }
            return {{}, "injected missing Renderer Global Shader"};
        }

      private:
        std::vector<toy3d::ShaderMapProgramData> programs_;
    };

    toy3d::ShaderMapProgramData make_imgui_program()
    {
        const toy3d::ShaderParametersMetadata& metadata = toy3d::imgui_global_shader_type().parameter_metadata();
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/UI/ImGui";
        program.pass_name = "ImGui";
        program.mapping_version = toy3d::shader::vulkan_binding_mapping_version;
        program.logical_layout_hash = nonzero_hash(10u);
        program.target_binding_hash = nonzero_hash(11u);
        // These authored passes have color-only targets. A mock Program must
        // carry the same state because Renderer now validates source-owned state.
        program.graphics_pass_state.cull_mode = toy3d::shader::ShaderGraphicsPassState::CullMode::None;
        program.graphics_pass_state.depth_test_enable = false;
        program.graphics_pass_state.depth_write_enable = false;
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = toy3d::shader::default_shader_permutation_key;

        toy3d::ShaderMapBinding constants;
        constants.parameter_id = metadata.constant_buffer.binding_id;
        constants.name = metadata.constant_buffer.name;
        constants.group = toy3d::RHIBindingGroup::Pass;
        constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        constants.stages = toy3d::RHIShaderStageFlags::Vertex;
        constants.target_binding = 0u;
        constants.constant_buffer_size = metadata.constant_buffer.size;
        const toy3d::ShaderParameterConstantMemberMetadata& projection = metadata.constant_buffer.members[0u];
        constants.constant_members.push_back({projection.parameter_id, projection.name,
                                              static_cast<toy3d::ShaderValueType>(projection.type), projection.offset,
                                              projection.size, projection.array_stride, projection.matrix_stride});
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
        vertex.binary = {1u, 2u, 3u, 10u};
        vertex.content_hash = nonzero_hash(12u);
        vertex.reflection.push_back(constants);
        vertex.interface_variables.push_back({"in.var.POSITION0", "POSITION0", 0u, true,
                                              toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 2u});
        vertex.interface_variables.push_back({"in.var.TEXCOORD0", "TEXCOORD0", 1u, true,
                                              toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 2u});
        vertex.interface_variables.push_back(
            {"in.var.COLOR0", "COLOR0", 2u, true, toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u});
        program.stages.push_back(std::move(vertex));
        program.vertex_inputs.push_back({toy3d::ShaderVertexAttributeId::Position0, "POSITION", 0u,
                                         toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 2u, 0u});
        program.vertex_inputs.push_back({toy3d::ShaderVertexAttributeId::TexCoord0, "TEXCOORD", 0u,
                                         toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 2u, 1u});
        program.vertex_inputs.push_back({toy3d::ShaderVertexAttributeId::Color0, "COLOR", 0u,
                                         toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32, 4u, 2u});
        toy3d::ShaderMapStage pixel;
        pixel.stage = toy3d::RHIShaderStage::Pixel;
        pixel.entry_point = "ps_main";
        pixel.binary = {4u, 3u, 2u, 10u};
        pixel.content_hash = nonzero_hash(13u);
        pixel.reflection = {texture, sampler};
        program.stages.push_back(std::move(pixel));
        finalize_generated_global_program(program, metadata);
        return program;
    }

    std::shared_ptr<const toy3d::GlobalShaderMap> make_global_shader_map(bool include_imgui = false)
    {
        const toy3d::ShaderParametersMetadata& metadata = toy3d::tonemap_global_shader_type().parameter_metadata();
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/PostProcess/Tonemap";
        program.pass_name = "Tonemap";
        program.mapping_version = toy3d::shader::vulkan_binding_mapping_version;
        program.logical_layout_hash = nonzero_hash(1u);
        program.target_binding_hash = nonzero_hash(2u);
        // These authored passes have color-only targets. A mock Program must
        // carry the same state because Renderer now validates source-owned state.
        program.graphics_pass_state.cull_mode = toy3d::shader::ShaderGraphicsPassState::CullMode::None;
        program.graphics_pass_state.depth_test_enable = false;
        program.graphics_pass_state.depth_write_enable = false;
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
        vertex.binary = {1u, 2u, 3u, 4u};
        vertex.content_hash = nonzero_hash(4u);
        program.stages.push_back(std::move(vertex));
        toy3d::ShaderMapStage pixel;
        pixel.stage = toy3d::RHIShaderStage::Pixel;
        pixel.entry_point = "ps_main";
        pixel.binary = {4u, 3u, 2u, 1u};
        pixel.content_hash = nonzero_hash(5u);
        pixel.reflection = program.bindings;
        program.stages.push_back(std::move(pixel));

        finalize_generated_global_program(program, metadata);
        std::vector<toy3d::ShaderMapProgramData> programs;
        programs.push_back(std::move(program));
        if (include_imgui)
        {
            programs.push_back(make_imgui_program());
        }
        RendererProgramLoader loader(std::move(programs));
        toy3d::ShaderMap shader_map(loader);
        toy3d::GlobalShaderMapResult result = toy3d::GlobalShaderMap::load(
            shader_map, toy3d::ShaderPlatform::VulkanES31,
            include_imgui ? std::vector<const toy3d::GlobalShaderType*>{&toy3d::tonemap_global_shader_type(),
                                                                        &toy3d::imgui_global_shader_type()}
                          : std::vector<const toy3d::GlobalShaderType*>{&toy3d::tonemap_global_shader_type()});
        return result.shader_map;
    }

    std::shared_ptr<const toy3d::GlobalShaderMap> make_empty_global_shader_map()
    {
        RendererProgramLoader loader(std::vector<toy3d::ShaderMapProgramData>{});
        toy3d::ShaderMap shader_map(loader);
        toy3d::GlobalShaderMapResult result =
            toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {});
        return result.shader_map;
    }

    template <typename MemberDescription, typename MemberDescription::type Member> struct PrivateMemberAccess
    {
        friend typename MemberDescription::type get(MemberDescription)
        {
            return Member;
        }
    };

    // Test-only access keeps the production ForwardSceneRenderer contract closed
    // while exercising the CPU stages directly.
    struct ForwardInitViewsMember
    {
        using type = bool (toy3d::ForwardSceneRenderer::*)();
        friend type get(ForwardInitViewsMember);
    };
    template struct PrivateMemberAccess<ForwardInitViewsMember, &toy3d::ForwardSceneRenderer::init_views>;

    struct SceneRendererViewInfosMember
    {
        using type = std::vector<toy3d::ViewInfo>& (toy3d::SceneRenderer::*)();
        friend type get(SceneRendererViewInfosMember);
    };
    template struct PrivateMemberAccess<SceneRendererViewInfosMember, &toy3d::SceneRenderer::view_infos>;

    struct ViewInfoShaderParametersMember
    {
        using type = toy3d::ViewShaderParameters toy3d::ViewInfo::*;
        friend type get(ViewInfoShaderParametersMember);
    };
    template struct PrivateMemberAccess<ViewInfoShaderParametersMember, &toy3d::ViewInfo::view_shader_parameters_>;

    static_assert(std::is_abstract<toy3d::SceneInterface>::value, "SceneInterface must remain an abstract bridge");
    static_assert(std::has_virtual_destructor<toy3d::SceneInterface>::value,
                  "SceneInterface must support polymorphic destruction");
    static_assert(std::is_base_of<toy3d::SceneInterface, toy3d::RenderScene>::value,
                  "RenderScene must expose only the SceneInterface bridge to Game-side code");
    static_assert(
        std::is_constructible<toy3d::RenderScene, toy3d::TaskGraphInterface&, toy3d::RenderResourceManager&>::value,
        "RenderScene construction must receive the logical-thread and resource-manager contracts");
    static_assert(std::is_constructible<toy3d::Renderer, toy3d::TaskGraphInterface&, toy3d::RHISurfaceRef,
                                        toy3d::RHIViewportContextDesc,
                                        std::function<toy3d::RHIResult<std::unique_ptr<toy3d::RHIDevice>>()>,
                                        std::shared_ptr<const toy3d::GlobalShaderMap>>::value,
                  "Engine must provide Renderer bootstrap inputs without owning RHI state");
    static_assert(!std::is_copy_constructible<toy3d::Renderer>::value, "Renderer ownership must remain unique");
    static_assert(!std::is_move_constructible<toy3d::Renderer>::value, "Renderer address must remain stable");
    static_assert(!std::is_copy_constructible<toy3d::RenderScene>::value, "RenderScene ownership must remain unique");
    static_assert(!std::is_move_constructible<toy3d::RenderScene>::value, "RenderScene address must remain stable");
    static_assert(!std::is_copy_constructible<toy3d::SceneViewFamily>::value,
                  "SceneViewFamily must retain one-shot ownership");
    static_assert(std::is_move_constructible<toy3d::SceneViewFamily>::value,
                  "SceneViewFamily must transfer into SceneRenderer ownership");
    static_assert(std::is_abstract<toy3d::SceneRenderer>::value,
                  "SceneRenderer must remain the polymorphic one-shot render owner");
    static_assert(std::has_virtual_destructor<toy3d::SceneRenderer>::value,
                  "SceneRenderer must support polymorphic logical-RT destruction");
    static_assert(std::is_final<toy3d::ForwardSceneRenderer>::value,
                  "ForwardSceneRenderer must remain the concrete forward implementation");
    static_assert(std::is_standard_layout<toy3d::ViewShaderParameters>::value,
                  "Generated View parameters must remain a standard-layout CPU value");
    static_assert(std::is_standard_layout<toy3d::ObjectShaderParameters>::value,
                  "Generated Object parameters must remain a standard-layout CPU value");

    int failure_count = 0;

    class RendererTestCommandList final : public toy3d::RHICommandList
    {
      public:
        explicit RendererTestCommandList(const toy3d::RHIDevice& owner)
            : RHICommandList(owner, "RendererTestCommandList")
        {
        }

        toy3d::RHIStatus begin()
        {
            return mark_recording();
        }

        toy3d::RHIStatus close()
        {
            return mark_closed();
        }
    };

    class RendererTestCommandContext final : public toy3d::RHIGraphicsCommandContext
    {
      public:
        using toy3d::RHIGraphicsCommandContext::RHIGraphicsCommandContext;

        std::uint32_t view_upload_count = 0u;
        std::vector<toy3d::RHIResourceTransition> recorded_transitions;
        std::vector<toy3d::RHIRenderPassDesc> recorded_passes;

        toy3d::RHIStatus begin_recording(const std::string&) override
        {
            command_list_ = std::make_shared<RendererTestCommandList>(*owner_device());
            return command_list_->begin();
        }

        toy3d::RHIStatus transition_resources_impl(
            const std::vector<toy3d::RHIResourceTransition>& transitions) override
        {
            recorded_transitions.insert(recorded_transitions.end(), transitions.begin(), transitions.end());
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_buffer_impl(const toy3d::RHIBufferCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_buffer_impl(const toy3d::RHIBufferUploadDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIResult<toy3d::RHIUniformBufferSlice> upload_transient_uniform_data_impl(
            const toy3d::RHITransientUniformDataDesc& desc) override
        {
            if (desc.source.size == 416u)
            {
                ++view_upload_count;
            }
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
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_texture_impl(const toy3d::RHITextureUploadDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus write_gpu_fence_impl(const toy3d::RHIGPUFenceRef&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIResult<toy3d::RHICommandListRef> finish_recording() override
        {
            const toy3d::RHIStatus closed = command_list_->close();
            if (!closed)
            {
                return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(closed.code(), closed.message());
            }
            return toy3d::RHIResult<toy3d::RHICommandListRef>::success(std::move(command_list_));
        }

        toy3d::RHIStatus begin_render_pass_impl(const toy3d::RHIRenderPassDesc& desc) override
        {
            recorded_passes.push_back(desc);
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus end_render_pass() override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus set_graphics_pipeline_impl(const toy3d::RHIGraphicsPipelineRef&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus set_viewport(const toy3d::RHIViewport&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus set_scissor(const toy3d::RHIRect&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus set_blend_constants(const toy3d::vec4&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus set_stencil_reference(std::uint8_t) override
        {
            return toy3d::RHIStatus::success();
        }
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
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus draw_indexed(const toy3d::RHIDrawIndexedArgs&) override
        {
            return toy3d::RHIStatus::success();
        }

      protected:
        toy3d::RHIStatus bind_graphics_bindings_impl(const toy3d::RHIGraphicsBindings&) override
        {
            return toy3d::RHIStatus::success();
        }

      private:
        std::shared_ptr<RendererTestCommandList> command_list_;
    };

    enum class RendererBootstrapFailurePoint
    {
        None,
        DeviceFactory,
        DeviceInitialize,
        MissingGlobalShader,
        ShaderProgram,
        BindingLayout,
        PassPipeline,
        PlaceholderTexture,
        PlaceholderView,
        PlaceholderSampler,
        CommandContext,
        QueueSubmit,
        QueueWait,
        Viewport
    };

    struct RendererDeviceProbe
    {
        std::atomic<toy3d::RHIErrorCode> frame_error{toy3d::RHIErrorCode::DeviceLost};
        bool validation_enabled = false;
        std::uint32_t wait_idle_before_shutdown_count = 0u;
        std::uint32_t shutdown_count = 0u;
    };

    class RendererTestQueue final : public toy3d::RHIQueue
    {
      public:
        RendererTestQueue(const toy3d::RHIDevice& owner, RendererBootstrapFailurePoint failure_point)
            : RHIQueue(owner), failure_point_(failure_point)
        {
        }

        toy3d::RHIQueueCompletionValue completed_value() const override
        {
            return completion_value_;
        }

        toy3d::RHIStatus wait_for_value(toy3d::RHIQueueCompletionValue value) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::QueueWait)
            {
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::BackendFailure,
                                                 "injected Renderer bootstrap queue wait failure");
            }
            return value <= completion_value_ ? toy3d::RHIStatus::success()
                                              : toy3d::RHIStatus::failure(toy3d::RHIErrorCode::NotReady,
                                                                          "Renderer test completion was not submitted");
        }

        toy3d::RHIStatus wait_idle() override
        {
            return toy3d::RHIStatus::success();
        }

      protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(const toy3d::RHISubmitInfo&) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::QueueSubmit)
            {
                return toy3d::RHIResult<toy3d::RHISubmitResult>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap queue submit failure");
            }
            ++completion_value_;
            toy3d::RHISubmitResult result;
            result.completion_value = completion_value_;
            return toy3d::RHIResult<toy3d::RHISubmitResult>::success(result);
        }

      private:
        RendererBootstrapFailurePoint failure_point_ = RendererBootstrapFailurePoint::None;
        toy3d::RHIQueueCompletionValue completion_value_ = 0;
    };

    class RendererTestViewport final : public toy3d::RHIViewportContext
    {
      public:
        RendererTestViewport(const toy3d::RHIDevice& device, std::shared_ptr<RendererDeviceProbe> probe)
            : RHIViewportContext(device), probe_(std::move(probe))
        {
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>> begin_frame_impl() override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>>::failure(
                probe_ ? probe_->frame_error.load() : toy3d::RHIErrorCode::DeviceLost,
                "injected Renderer frame failure");
        }

        toy3d::RHIResult<toy3d::RHIFrameEndResult> end_frame(std::unique_ptr<toy3d::RHIFrameContext>,
                                                             const std::vector<toy3d::RHICommandListRef>&) override
        {
            return toy3d::RHIResult<toy3d::RHIFrameEndResult>::failure(toy3d::RHIErrorCode::InvalidArgument,
                                                                       "Renderer lifecycle smoke does not end frames");
        }

        toy3d::RHIStatus abort_frame(std::unique_ptr<toy3d::RHIFrameContext>) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus request_resize(const toy3d::Extent&) override
        {
            return toy3d::RHIStatus::success();
        }

      private:
        std::shared_ptr<RendererDeviceProbe> probe_;
    };

    class RendererTestDevice final : public toy3d::RHIDevice
    {
      public:
        explicit RendererTestDevice(RendererBootstrapFailurePoint failure_point = RendererBootstrapFailurePoint::None,
                                    std::shared_ptr<RendererDeviceProbe> probe = nullptr, bool fail_shutdown = false)
            : queue_(*this, failure_point), failure_point_(failure_point), probe_(std::move(probe)),
              fail_shutdown_(fail_shutdown)
        {
            limits_.max_color_attachments = std::numeric_limits<std::uint32_t>::max();
            limits_.max_vertex_buffers = std::numeric_limits<std::uint32_t>::max();
            limits_.max_texture_dimension_2d = std::numeric_limits<std::uint32_t>::max();
            limits_.max_texture_array_layers = std::numeric_limits<std::uint32_t>::max();
            limits_.max_uniform_buffer_size = std::numeric_limits<std::uint32_t>::max();
            limits_.max_binding_slots_per_group = std::numeric_limits<std::uint32_t>::max();
            limits_.max_sampler_anisotropy = std::numeric_limits<std::uint32_t>::max();
        }

        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc& desc) override
        {
            if (probe_)
            {
                probe_->validation_enabled = desc.enable_validation;
            }
            if (failure_point_ == RendererBootstrapFailurePoint::DeviceInitialize)
            {
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::BackendFailure,
                                                 "injected Renderer bootstrap device initialize failure");
            }
            initialized_ = toy3d::validate_device_desc(desc).succeeded();
            return initialized_ ? toy3d::RHIStatus::success()
                                : toy3d::RHIStatus::failure(toy3d::RHIErrorCode::InvalidArgument,
                                                            "Renderer test device rejected its surface");
        }
        const toy3d::RHICapabilities& capabilities() const override
        {
            return capabilities_;
        }
        const toy3d::RHILimits& limits() const override
        {
            return limits_;
        }
        void set_texture_dimension_limit(std::uint32_t limit)
        {
            limits_.max_texture_dimension_2d = limit;
        }
        toy3d::RHIFormatCapabilities format_capabilities(toy3d::PixelFormat) const override
        {
            toy3d::RHIFormatCapabilities result;
            result.usage = toy3d::RHIFormatUsage::Sampled | toy3d::RHIFormatUsage::Storage |
                           toy3d::RHIFormatUsage::RenderTarget | toy3d::RHIFormatUsage::DepthStencil |
                           toy3d::RHIFormatUsage::VertexBuffer | toy3d::RHIFormatUsage::CopySource |
                           toy3d::RHIFormatUsage::CopyDestination;
            return result;
        }
        toy3d::RHIQueue& graphics_queue() override
        {
            return queue_;
        }
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>> create_viewport_context_impl(
            const toy3d::RHISurfaceRef&, const toy3d::RHIViewportContextDesc&) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::Viewport)
            {
                return toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap viewport failure");
            }
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>::success(
                std::make_unique<RendererTestViewport>(*this, probe_));
        }
        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer_impl(const toy3d::RHIBufferDesc&,
                                                                 const toy3d::RHIInitialData*) override
        {
            return toy3d::RHIResult<toy3d::RHIBufferRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                  "unused test buffer");
        }
        toy3d::RHIResult<toy3d::RHITextureRef> create_texture_impl(const toy3d::RHITextureDesc& desc,
                                                                   const toy3d::RHIInitialData*) override
        {
            ++texture_create_count_;
            if (failure_point_ == RendererBootstrapFailurePoint::PlaceholderTexture && texture_create_count_ == 1u)
            {
                return toy3d::RHIResult<toy3d::RHITextureRef>::failure(toy3d::RHIErrorCode::BackendFailure,
                                                                       "injected Renderer bootstrap texture failure");
            }
            return toy3d::RHIResult<toy3d::RHITextureRef>::success(std::make_shared<toy3d::RHITexture>(*this, desc));
        }
        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view_impl(const toy3d::RHIBufferRef&,
                                                                          const toy3d::RHIBufferViewDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIBufferViewRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                      "unused test buffer view");
        }
        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view_impl(
            const toy3d::RHITextureRef& texture, const toy3d::RHITextureViewDesc& desc) override
        {
            ++texture_view_create_count_;
            if (failure_point_ == RendererBootstrapFailurePoint::PlaceholderView && texture_view_create_count_ == 1u)
            {
                return toy3d::RHIResult<toy3d::RHITextureViewRef>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap texture view failure");
            }
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                std::make_shared<toy3d::RHITextureView>(texture, desc));
        }
        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler_impl(const toy3d::RHISamplerDesc& desc) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::PlaceholderSampler)
            {
                return toy3d::RHIResult<toy3d::RHISamplerRef>::failure(toy3d::RHIErrorCode::BackendFailure,
                                                                       "injected Renderer bootstrap sampler failure");
            }
            return toy3d::RHIResult<toy3d::RHISamplerRef>::success(std::make_shared<toy3d::RHISampler>(*this, desc));
        }
        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence_impl(const std::string&) override
        {
            return toy3d::RHIResult<toy3d::RHIGPUFenceRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                    "unused test fence");
        }
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>> create_graphics_command_context_impl()
            override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::CommandContext)
            {
                return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap command context failure");
            }
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::success(
                std::make_unique<RendererTestCommandContext>(*this));
        }

      protected:
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(const toy3d::RHIShaderDesc& desc) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::ShaderProgram)
            {
                return toy3d::RHIResult<toy3d::RHIShaderRef>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap Shader Program failure");
            }
            return toy3d::RHIResult<toy3d::RHIShaderRef>::success(std::make_shared<toy3d::RHIShader>(*this, desc));
        }
        toy3d::RHIResult<toy3d::RHIBindingLayoutRef> create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc& desc) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::BindingLayout)
            {
                return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap binding layout failure");
            }
            return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::success(
                std::make_shared<toy3d::RHIBindingLayout>(*this, desc));
        }
        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc& desc) override
        {
            if (failure_point_ == RendererBootstrapFailurePoint::PassPipeline)
            {
                return toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>::failure(
                    toy3d::RHIErrorCode::BackendFailure, "injected Renderer bootstrap Pass pipeline failure");
            }
            return toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>::success(
                std::make_shared<toy3d::RHIGraphicsPipeline>(*this, desc));
        }
        bool is_initialized_impl() const override
        {
            return initialized_;
        }
        toy3d::RHIStatus wait_idle_before_shutdown_impl() override
        {
            if (probe_)
            {
                ++probe_->wait_idle_before_shutdown_count;
            }
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus shutdown_impl() override
        {
            if (probe_)
            {
                ++probe_->shutdown_count;
            }
            initialized_ = false;
            if (fail_shutdown_)
            {
                return toy3d::RHIStatus::failure(toy3d::RHIErrorCode::BackendFailure,
                                                 "injected secondary terminal shutdown diagnostic");
            }
            return toy3d::RHIStatus::success();
        }

      private:
        RendererTestQueue queue_;
        toy3d::RHICapabilities capabilities_;
        toy3d::RHILimits limits_;
        RendererBootstrapFailurePoint failure_point_ = RendererBootstrapFailurePoint::None;
        std::shared_ptr<RendererDeviceProbe> probe_;
        bool fail_shutdown_ = false;
        std::uint32_t texture_create_count_ = 0u;
        std::uint32_t texture_view_create_count_ = 0u;
        bool initialized_ = false;
    };

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    std::unique_ptr<toy3d::TaskGraphInterface> create_graph(toy3d::ThreadManager& thread_manager, bool multithreaded)
    {
        toy3d::TaskGraphCreateResult created =
            toy3d::create_task_graph({multithreaded ? 1u : 0u, 64, multithreaded}, thread_manager);
        check(created.succeeded(), "Renderer fixture must create Task Graph");
        if (!created.succeeded())
        {
            return nullptr;
        }

        std::unique_ptr<toy3d::TaskGraphInterface> graph = created.take_task_graph();
        check(graph->attach_to_thread(toy3d::NamedThread::GameThread).succeeded(),
              "Renderer fixture must attach GameThread");
        return graph;
    }

    void shutdown_graph(std::unique_ptr<toy3d::TaskGraphInterface>& graph)
    {
        if (graph)
        {
            check(graph->shutdown(toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
                  "Renderer fixture Task Graph must shut down");
            graph.reset();
        }
    }

    toy3d::StaticMeshRef make_mesh(toy3d::MaterialInstanceRef* out_material = nullptr)
    {
        toy3d::MaterialDesc material_desc;
        material_desc.shader_name = "Builtin/Surface/Phong";
        const toy3d::MaterialInstanceRef material =
            toy3d::MaterialInstance::create(toy3d::Material::create(std::move(material_desc)));
        if (out_material != nullptr)
        {
            *out_material = material;
        }

        toy3d::StaticMeshDesc mesh_desc;
        mesh_desc.vertices = {{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                              {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                              {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
        mesh_desc.indices = std::vector<std::uint16_t>{0, 1, 2};
        mesh_desc.sections.push_back({0, 3, 0});
        mesh_desc.material_slots.push_back(material);
        return toy3d::StaticMesh::create(std::move(mesh_desc));
    }

    toy3d::AxisAlignedBounds make_bounds(const toy3d::vec3& center, const toy3d::vec3& extent)
    {
        toy3d::AxisAlignedBounds bounds;
        bounds.minimum = center - extent;
        bounds.maximum = center + extent;
        return bounds;
    }

    toy3d::SceneView make_perspective_view(const toy3d::Vector3& camera_position,
                                           toy3d::CameraProjectionMode projection_mode, float near_clip, float far_clip)
    {
        return toy3d::SceneView(camera_position, toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                                toy3d::IntRect{0, 0, 128u, 128u}, toy3d::Extent{128u, 128u}, projection_mode,
                                toy3d::Radians(1.57079632679f), near_clip, far_clip);
    }

    toy3d::PrimitiveSceneProxy* add_visibility_proxy(toy3d::RenderScene& render_scene,
                                                     const toy3d::AxisAlignedBounds& bounds, bool visible)
    {
        auto proxy = std::make_unique<toy3d::StaticMeshSceneProxy>(toy3d::Matrix4::identity(), bounds, visible, nullptr,
                                                                   std::vector<toy3d::MaterialRenderProxy*>{});
        toy3d::PrimitiveSceneProxy* const raw_proxy = proxy.get();
        render_scene.add_primitive(std::move(proxy));
        return raw_proxy;
    }

    bool visible_contains(const toy3d::ViewInfo& view_info, const toy3d::PrimitiveSceneProxy* proxy)
    {
        for (const toy3d::PrimitiveSceneInfo* const primitive_info : view_info.visible_primitives())
        {
            if (primitive_info != nullptr && primitive_info->proxy() == proxy)
            {
                return true;
            }
        }
        return false;
    }

    bool init_views(toy3d::ForwardSceneRenderer& renderer)
    {
        return (renderer.*get(ForwardInitViewsMember{}))();
    }

    std::vector<toy3d::ViewInfo>& view_infos(toy3d::ForwardSceneRenderer& renderer)
    {
        toy3d::SceneRenderer& base_renderer = renderer;
        return (base_renderer.*get(SceneRendererViewInfosMember{}))();
    }

    void compute_visibility(toy3d::ForwardSceneRenderer& renderer, const toy3d::RenderScene& render_scene)
    {
        toy3d::compute_scene_visibility(render_scene, view_infos(renderer));
    }

    void test_cpu_view_and_visibility()
    {
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, false);
        if (!graph)
        {
            return;
        }

        toy3d::RenderingThread rendering_thread(thread_manager, *graph, toy3d::RenderingThreadMode::SingleThread);
        check(rendering_thread.start().succeeded(), "CPU visibility fixture must start the logical Rendering Thread");

        {
            RendererTestDevice device;
            {
                RendererTestDevice shadow_device;
                toy3d::RHISurfaceDesc surface_desc;
                surface_desc.platform = toy3d::RHISurfacePlatform::Glfw;
                surface_desc.window_handle = reinterpret_cast<void*>(1);
                toy3d::RHIDeviceDesc device_desc;
                device_desc.primary_surface = std::make_shared<toy3d::RHISurface>(surface_desc);
                check(shadow_device.initialize(device_desc).succeeded(),
                      "Shadow target fixture must initialize its fake RHI device");
                toy3d::ShadowRenderTargets shadow_targets;
                for (std::uint32_t size : {512u, 1024u, 2048u})
                {
                    for (std::size_t count = 1u; count <= toy3d::LightSceneData::k_max_shadow_cascades; ++count)
                    {
                        check(shadow_targets.ensure_views(shadow_device, 2u, count, size).succeeded(),
                              "Every supported cascade count and maximum size must allocate an atlas");
                        const auto& layout = shadow_targets.layout();
                        check(layout.cascade_count == count && layout.max_resolution == size &&
                                  layout.width == (count == 1u ? size : size + size / 2u) && layout.height == size &&
                                  shadow_targets.texture(0u)->desc().width == layout.width &&
                                  shadow_targets.texture(0u)->desc().height == layout.height &&
                                  shadow_targets.texture(0u) != shadow_targets.texture(1u),
                              "Atlas allocation must match the layout and remain isolated per View");
                        for (std::size_t index = 0u; index < count; ++index)
                        {
                            const auto& tile = layout.tiles[index];
                            check(tile.size == (index == 0u ? size : size / 2u) &&
                                      tile.resolution() + 2u * toy3d::ShadowCascadeTile::k_border == tile.size &&
                                      tile.x + tile.size <= layout.width && tile.y + tile.size <= layout.height,
                                  "Each active tile must include its PCF guard inside atlas bounds");
                            for (std::size_t other = 0u; other < index; ++other)
                            {
                                const auto& neighbor = layout.tiles[other];
                                check(tile.x >= neighbor.x + neighbor.size || neighbor.x >= tile.x + tile.size ||
                                          tile.y >= neighbor.y + neighbor.size || neighbor.y >= tile.y + tile.size,
                                      "Cascade tiles must never overlap");
                            }
                        }
                        for (std::size_t index = count; index < toy3d::LightSceneData::k_max_shadow_cascades; ++index)
                        {
                            check(layout.tiles[index].size == 0u, "Inactive tiles must discard previous configuration");
                        }
                    }
                }
                check(shadow_targets.ensure_views(shadow_device, 2u, 1u, 2048u).succeeded(), "Single atlas setup");
                const toy3d::RHITextureRef retained_texture = shadow_targets.texture(0u);
                check(shadow_targets.ensure_views(shadow_device, 2u, 1u, 2048u).succeeded() &&
                          shadow_targets.texture(0u) == retained_texture,
                      "Unchanged shadow configuration must reuse GPU resources");
                check(shadow_targets.ensure_views(shadow_device, 2u, 2u, 2048u).succeeded() &&
                          shadow_targets.texture(0u) != retained_texture && retained_texture->desc().width == 2048u,
                      "Replacing an atlas must preserve references retained by prior frames");
                const auto two_cascade_texture = shadow_targets.texture(0u);
                shadow_targets.publish_submitted_access();
                check(shadow_targets.ensure_views(shadow_device, 2u, 3u, 2048u).succeeded() &&
                          shadow_targets.texture(0u) == two_cascade_texture &&
                          shadow_targets.access(0u) == toy3d::RHIAccess::ShaderResourceGraphics &&
                          shadow_targets.layout().tiles[2].size == 1024u,
                      "Two to three cascades must reuse the atlas and retain submitted access");
                check(shadow_targets.ensure_views(shadow_device, 2u, 2u, 2048u).succeeded() &&
                          shadow_targets.texture(0u) == two_cascade_texture &&
                          shadow_targets.layout().tiles[2].size == 0u,
                      "Three to two cascades must clear inactive metadata without reallocating");
                // An inactive View still clears all atlas tiles to deterministic fully lit depth.
                toy3d::RenderResourceManager atlas_resource_manager(shadow_device);
                toy3d::RenderScene atlas_scene(*graph, atlas_resource_manager);
                std::vector<toy3d::SceneView> atlas_views;
                atlas_views.push_back(
                    make_perspective_view(toy3d::Vector3(), toy3d::CameraProjectionMode::Perspective, 0.1f, 10.0f));
                toy3d::ForwardSceneRenderer atlas_renderer(
                    toy3d::SceneViewFamily(atlas_scene, toy3d::Extent{128u, 128u}, std::move(atlas_views)));
                check(init_views(atlas_renderer), "Atlas recording fixture must initialize its View");
                check(shadow_targets.ensure_views(shadow_device, 2u, 3u, 2048u).succeeded(),
                      "Three cascade recording setup");
                RendererTestCommandContext atlas_context(shadow_device);
                check(atlas_context.begin_recording("AtlasClear").succeeded(), "Atlas fixture recording begins");
                toy3d::RHIShaderProgramCache atlas_program_cache(shadow_device);
                check(toy3d::render_shadow_pass(shadow_device, atlas_program_cache, atlas_context,
                                                view_infos(atlas_renderer)[0], shadow_targets, 0u, nullptr)
                              .succeeded() &&
                          atlas_context.recorded_passes.size() == 1u &&
                          atlas_context.recorded_transitions.size() == 2u &&
                          atlas_context.recorded_passes[0].depth_stencil_attachment.depth_load ==
                              toy3d::RHILoadOperation::Clear &&
                          atlas_context.recorded_passes[0].depth_stencil_attachment.clear_value.depth == 0.0f &&
                          atlas_context.recorded_passes[0].depth_stencil_attachment.view ==
                              shadow_targets.depth_view(0u) &&
                          atlas_context.recorded_transitions[0].resource == shadow_targets.texture(0u) &&
                          atlas_context.recorded_transitions[1].after == toy3d::RHIAccess::ShaderResourceGraphics,
                      "Atlas must clear reversed-Z once and transition the whole shared texture to shader read");
                check(atlas_context.finish_recording().succeeded(), "Atlas fixture recording ends");
                shadow_device.set_texture_dimension_limit(2048u);
                check(shadow_targets.ensure_views(shadow_device, 2u, 3u, 2048u).succeeded() &&
                          shadow_targets.layout().max_resolution == 1024u && shadow_targets.layout().width == 1536u &&
                          shadow_targets.layout().cascade_count == 3u &&
                          shadow_targets.access(0u) == toy3d::RHIAccess::Common,
                      "Device fallback must consider the entire atlas width and preserve cascade count");
                shadow_device.set_texture_dimension_limit(512u);
                check(shadow_targets.ensure_views(shadow_device, 2u, 1u, 2048u).succeeded() &&
                          shadow_targets.layout().max_resolution == 512u,
                      "Single cascade may use a 512 device limit");
                const auto supported_texture = shadow_targets.texture(0u);
                const auto unsupported_shadow = shadow_targets.ensure_views(shadow_device, 2u, 3u, 2048u);
                check(!unsupported_shadow && unsupported_shadow.code() == toy3d::RHIErrorCode::Unsupported &&
                          shadow_targets.texture(0u) == supported_texture,
                      "Unsupported atlas dimensions must fail without destroying previous resources");
                check(!shadow_targets.ensure_views(shadow_device, 2u, 1u, 256u) &&
                          !shadow_targets.ensure_views(shadow_device, 2u, 1u, 4096u),
                      "Only the three configured maximum-size choices are valid");
                shadow_targets.release();
            }
            toy3d::RenderResourceManager resource_manager(device);
            toy3d::RenderScene render_scene(*graph, resource_manager);

            toy3d::PrimitiveSceneProxy* const inside =
                add_visibility_proxy(render_scene, make_bounds({0.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f}), true);
            toy3d::PrimitiveSceneProxy* const far_only =
                add_visibility_proxy(render_scene, make_bounds({0.0f, 0.0f, 50.0f}, {0.25f, 0.25f, 0.25f}), true);
            toy3d::PrimitiveSceneProxy* const outside =
                add_visibility_proxy(render_scene, make_bounds({20.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f}), true);
            toy3d::PrimitiveSceneProxy* const touching_near =
                add_visibility_proxy(render_scene, make_bounds({0.0f, 0.0f, 0.2f}, {0.1f, 0.1f, 0.1f}), true);
            toy3d::PrimitiveSceneProxy* const hidden =
                add_visibility_proxy(render_scene, make_bounds({0.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f}), false);
            toy3d::PrimitiveSceneProxy* const second_view_only =
                add_visibility_proxy(render_scene, make_bounds({100.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f}), true);
            toy3d::AxisAlignedBounds invalid_bounds = make_bounds({0.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f});
            invalid_bounds.minimum.x = std::numeric_limits<float>::quiet_NaN();
            toy3d::PrimitiveSceneProxy* const invalid_bounds_proxy =
                add_visibility_proxy(render_scene, invalid_bounds, true);
            std::vector<toy3d::SceneView> finite_views;
            finite_views.push_back(make_perspective_view(toy3d::Vector3(0.0f, 0.0f, 0.0f),
                                                         toy3d::CameraProjectionMode::Perspective, 0.1f, 10.0f));
            finite_views.push_back(make_perspective_view(toy3d::Vector3(100.0f, 0.0f, 0.0f),
                                                         toy3d::CameraProjectionMode::Perspective, 0.1f, 10.0f));
            toy3d::ForwardSceneRenderer finite_renderer(
                toy3d::SceneViewFamily(render_scene, toy3d::Extent{128u, 128u}, std::move(finite_views)));
            check(init_views(finite_renderer) && view_infos(finite_renderer).size() == 2u &&
                      view_infos(finite_renderer)[0].visible_primitives().empty() &&
                      view_infos(finite_renderer)[1].visible_primitives().empty(),
                  "init_views must build two independent ViewInfo values with empty current-frame visibility");
            toy3d::LightSceneData shadow_light;
            shadow_light.cast_shadows = true;
            check(shadow_light.shadow_cascade_count == 1, "Directional shadows default to one cascade");
            shadow_light.shadow_cascade_count = 2;
            shadow_light.shadow_distance = 6.0f;
            shadow_light.direction = toy3d::Vector3(0.0f, -1.0f, 0.0f);
            const toy3d::RHIStatus shadow_status =
                toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer));
            check(shadow_status.succeeded() && view_infos(finite_renderer)[0].shadow_active() &&
                      view_infos(finite_renderer)[1].shadow_active() &&
                      toy3d::is_finite(view_infos(finite_renderer)[0].shadow_cascade(0u).world_to_clip) &&
                      toy3d::is_finite(view_infos(finite_renderer)[0].shadow_cascade(1u).world_to_clip) &&
                      view_infos(finite_renderer)[0].shadow_cascade(0u).far_distance >
                          view_infos(finite_renderer)[0].shadow_cascade(1u).near_distance &&
                      view_infos(finite_renderer)[0].shadow_split(0u).x <
                          view_infos(finite_renderer)[0].shadow_split(0u).y &&
                      view_infos(finite_renderer)[0].shadow_effective_end() == 6.0f &&
                      view_infos(finite_renderer)[0].shadow_fade_start() > 5.0f &&
                      view_infos(finite_renderer)[0].shadow_fade_start() < 6.0f,
                  "near-vertical light direction must produce finite per-view shadow bases and finite distance");
            for (const toy3d::ViewInfo& view : view_infos(finite_renderer))
            {
                for (std::size_t cascade_index = 0u; cascade_index < view.shadow_cascade_count(); ++cascade_index)
                {
                    const toy3d::ShadowCascadeInfo& cascade = view.shadow_cascade(cascade_index);
                    check(toy3d::is_finite(cascade.transition_scale) && cascade.transition_scale > 0.0f,
                          "Every cascade must provide a finite receiver depth transition");
                    const float distance = (cascade.near_distance + cascade.far_distance) * 0.5f;
                    const toy3d::Vector3 receiver =
                        view.scene_view().camera_position() + toy3d::Vector3(0.0f, 0.0f, distance);
                    const toy3d::Vector4 clip = cascade.world_to_clip * toy3d::Vector4(receiver, 1.0f);
                    check(clip.w > 0.0f && std::abs(clip.x / clip.w) < 1.0f && std::abs(clip.y / clip.w) < 1.0f &&
                              clip.z / clip.w >= 0.0f && clip.z / clip.w <= 1.0f,
                          "each cascade must project its receiver segment into the shadow depth map");
                }
            }
            const float two_cascade_bias = view_infos(finite_renderer)[0].shadow_cascade(0u).bias_parameters.x;
            for (int count = 1; count <= toy3d::LightSceneData::k_max_shadow_cascades; ++count)
            {
                shadow_light.shadow_cascade_count = count;
                check(toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer))
                          .succeeded(),
                      "All configured cascade counts must be supported");
                for (const toy3d::ViewInfo& view : view_infos(finite_renderer))
                {
                    check(view.shadow_active() && view.shadow_cascade_count() == static_cast<std::size_t>(count) &&
                              view.shadow_cascade(0u).near_distance == view.scene_view().near_clip() &&
                              view.shadow_cascade(count - 1u).far_distance == 6.0f,
                          "Active cascades must cover the full requested shadow range for each view");
                    for (std::size_t index = 0; index + 1u < view.shadow_cascade_count(); ++index)
                    {
                        check(view.shadow_cascade(index).far_distance == view.shadow_split(index).y &&
                                  view.shadow_cascade(index + 1u).near_distance == view.shadow_split(index).x,
                              "Neighboring cascades must exactly cover their shared blend interval");
                    }
                    for (std::size_t index = view.shadow_cascade_count();
                         index < toy3d::ShadowRenderTargets::k_max_cascade_count; ++index)
                    {
                        check(view.shadow_cascade(index).batches.empty() &&
                                  view.shadow_cascade(index).transition_scale == 0,
                              "Reducing cascade count must clear stale inactive cascade data");
                    }
                }
            }
            const toy3d::ViewInfo& three_cascade_view = view_infos(finite_renderer)[0];
            const float span = 6.0f - three_cascade_view.scene_view().near_clip();
            check(std::abs((three_cascade_view.shadow_split(0u).x + three_cascade_view.shadow_split(0u).y) * 0.5f -
                           (0.1f + span / 13.0f)) < 1e-5f &&
                      std::abs((three_cascade_view.shadow_split(1u).x + three_cascade_view.shadow_split(1u).y) * 0.5f -
                               (0.1f + span * 4.0f / 13.0f)) < 1e-5f &&
                      three_cascade_view.shadow_split(0u).y < three_cascade_view.shadow_split(1u).x &&
                      three_cascade_view.shadow_cascade(0u).bias_parameters.x < two_cascade_bias,
                  "UE geometric distribution 1:3:9 must improve near texel precision with disjoint blend intervals");
            shadow_light.cascade_distribution_exponent = 1.0f;
            check(toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer))
                          .succeeded() &&
                      std::abs((view_infos(finite_renderer)[0].shadow_split(0u).x +
                                view_infos(finite_renderer)[0].shadow_split(0u).y) *
                                   0.5f -
                               (0.1f + span / 3.0f)) < 1e-5f,
                  "Distribution exponent one must produce uniform segments");
            for (const float exponent : {0.1f, 10.0f})
            {
                shadow_light.cascade_distribution_exponent = exponent;
                check(toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer))
                              .succeeded() &&
                          view_infos(finite_renderer)[0].shadow_split(0u).x > 0.1f &&
                          view_infos(finite_renderer)[0].shadow_split(0u).y <
                              view_infos(finite_renderer)[0].shadow_split(1u).x &&
                          view_infos(finite_renderer)[0].shadow_split(1u).y < 6.0f,
                      "Extreme valid exponents must keep blend intervals inside the range and separate");
            }
            shadow_light.cascade_distribution_exponent = 3.0f;
            shadow_light.shadow_cascade_count = 2;
            shadow_light.shadow_map_resolution = 1024;
            check(
                toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer)).succeeded(),
                "Low resolution visibility setup");
            std::array<float, toy3d::LightSceneData::k_max_shadow_cascades> low_resolution_bias{};
            for (std::size_t index = 0u; index < view_infos(finite_renderer)[0].shadow_cascade_count(); ++index)
            {
                low_resolution_bias[index] = view_infos(finite_renderer)[0].shadow_cascade(index).bias_parameters.x;
            }
            shadow_light.shadow_map_resolution = 2048;
            check(
                toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer)).succeeded(),
                "High resolution visibility setup");
            for (std::size_t index = 0u; index < view_infos(finite_renderer)[0].shadow_cascade_count(); ++index)
            {
                check(view_infos(finite_renderer)[0].shadow_cascade(index).bias_parameters.x <
                          low_resolution_bias[index],
                      "Increasing maximum size must reduce world-texel bias in every cascade");
            }
            shadow_light.shadow_cascade_count = 4;
            check(!toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer)),
                  "Render visibility must reject a cascade count beyond supported capacity");
            shadow_light.shadow_cascade_count = 2;
            shadow_light.shadow_bias = 0.0f;
            check(toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer))
                          .succeeded() &&
                      toy3d::is_finite(view_infos(finite_renderer)[0].shadow_cascade(0u).transition_scale) &&
                      view_infos(finite_renderer)[0].shadow_cascade(0u).transition_scale > 0.0f,
                  "Zero caster bias must not produce an infinite receiver transition");
            shadow_light.cast_shadows = false;
            check(toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer))
                          .succeeded() &&
                      !view_infos(finite_renderer)[0].shadow_active() &&
                      !view_infos(finite_renderer)[1].shadow_active(),
                  "disabling the selected light's shadow must clear per-view shadow state");
            shadow_light.cast_shadows = true;
            shadow_light.shadow_distance = 0.0f;
            check(toy3d::compute_shadow_visibility(render_scene, &shadow_light, view_infos(finite_renderer))
                          .succeeded() &&
                      !view_infos(finite_renderer)[0].shadow_active(),
                  "zero dynamic shadow distance must disable cascade generation");

            std::vector<toy3d::SceneView> invalid_parameter_views;
            invalid_parameter_views.push_back(
                make_perspective_view(toy3d::Vector3(), toy3d::CameraProjectionMode::Perspective, 0.1f, 10.0f));
            invalid_parameter_views.push_back(make_perspective_view(
                toy3d::Vector3(1.0f, 0.0f, 0.0f), toy3d::CameraProjectionMode::Perspective, 0.1f, 10.0f));
            toy3d::ForwardSceneRenderer invalid_parameter_renderer(
                toy3d::SceneViewFamily(render_scene, toy3d::Extent{128u, 128u}, std::move(invalid_parameter_views)));
            check(init_views(invalid_parameter_renderer),
                  "invalid generated View parameter fixture must first initialize canonical CPU views");
            toy3d::ViewShaderParameters& invalid_parameters =
                view_infos(invalid_parameter_renderer)[1].*get(ViewInfoShaderParametersMember{});
            invalid_parameters.toy_view.at(0u, 0u) = std::numeric_limits<float>::quiet_NaN();
            RendererTestCommandContext invalid_view_context(device);
            const toy3d::RHIStatus invalid_binding_status = toy3d::create_view_shader_bindings(
                device, invalid_view_context, view_infos(invalid_parameter_renderer));
            check(!invalid_binding_status && invalid_binding_status.code() == toy3d::RHIErrorCode::InvalidArgument &&
                      invalid_view_context.view_upload_count == 0u &&
                      !view_infos(invalid_parameter_renderer)[0].view_binding() &&
                      !view_infos(invalid_parameter_renderer)[1].view_binding(),
                  "non-finite View matrices must fail the whole batch before any upload or partial publication");

            compute_visibility(finite_renderer, render_scene);
            check(visible_contains(view_infos(finite_renderer)[0], inside) &&
                      visible_contains(view_infos(finite_renderer)[0], touching_near) &&
                      !visible_contains(view_infos(finite_renderer)[0], far_only) &&
                      !visible_contains(view_infos(finite_renderer)[0], outside) &&
                      !visible_contains(view_infos(finite_renderer)[0], hidden) &&
                      !visible_contains(view_infos(finite_renderer)[0], invalid_bounds_proxy) &&
                      !visible_contains(view_infos(finite_renderer)[0], second_view_only),
                  "finite View visibility must include inside/touching AABBs and reject far, outside, and hidden "
                  "proxies");
            check(visible_contains(view_infos(finite_renderer)[1], second_view_only) &&
                      !visible_contains(view_infos(finite_renderer)[1], inside) &&
                      view_infos(finite_renderer)[1].mesh_batches().empty(),
                  "multi-view visibility must keep per-view results independent");

            check(init_views(finite_renderer) && view_infos(finite_renderer)[0].visible_primitives().empty(),
                  "reinitializing a renderer must reset previous visibility results");

            const std::uint64_t prior_object_generation = inside->object_data_generation();
            toy3d::Matrix4 updated_object_transform = toy3d::Matrix4::identity();
            updated_object_transform.at(3u, 0u) = 1.0f;
            render_scene.update_primitive_transform(inside, updated_object_transform,
                                                    make_bounds({0.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f}), false, true,
                                                    true);
            compute_visibility(finite_renderer, render_scene);
            check(!visible_contains(view_infos(finite_renderer)[0], inside) &&
                      visible_contains(view_infos(finite_renderer)[0], touching_near) &&
                      inside->object_shader_parameters().toy_object_to_world == updated_object_transform &&
                      inside->object_data_generation() == prior_object_generation + 1u,
                  "scene updates must advance Object data generation and clear stale visibility results");
            const std::uint64_t shadow_generation = inside->object_data_generation();
            render_scene.update_primitive_transform(inside, updated_object_transform,
                                                    make_bounds({0.0f, 0.0f, 4.0f}, {0.25f, 0.25f, 0.25f}), false, true,
                                                    false);
            check(inside->cast_shadows() && !inside->receives_shadows() &&
                      inside->object_data_generation() == shadow_generation + 1u,
                  "Receiver toggle must update CPU selection and generation independently of casting");

            std::vector<toy3d::SceneView> infinite_views;
            infinite_views.push_back(make_perspective_view(
                toy3d::Vector3(0.0f, 0.0f, 0.0f), toy3d::CameraProjectionMode::PerspectiveInfiniteFar, 0.1f, 0.0f));
            toy3d::ForwardSceneRenderer infinite_renderer(
                toy3d::SceneViewFamily(render_scene, toy3d::Extent{128u, 128u}, std::move(infinite_views)));
            check(init_views(infinite_renderer), "infinite-far View must initialize with a five-plane frustum");
            compute_visibility(infinite_renderer, render_scene);
            check(visible_contains(view_infos(infinite_renderer)[0], far_only),
                  "infinite-far visibility must not cull by a fabricated far plane");

            const auto invalid_view_rejected = [&](toy3d::SceneView view)
            {
                std::vector<toy3d::SceneView> invalid_views;
                invalid_views.push_back(std::move(view));
                toy3d::ForwardSceneRenderer invalid_renderer(
                    toy3d::SceneViewFamily(render_scene, toy3d::Extent{128u, 128u}, std::move(invalid_views)));
                return !init_views(invalid_renderer) && view_infos(invalid_renderer).empty();
            };
            check(invalid_view_rejected(toy3d::SceneView(
                      toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                      toy3d::IntRect{0, 0, 0u, 128u}, toy3d::Extent{128u, 128u},
                      toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 10.0f)),
                  "init_views must reject an empty view rect");
            check(invalid_view_rejected(toy3d::SceneView(
                      toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                      toy3d::IntRect{-1, 0, 128u, 128u}, toy3d::Extent{128u, 128u},
                      toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 10.0f)),
                  "init_views must reject a negative view rect origin");
            check(invalid_view_rejected(
                      make_perspective_view(toy3d::Vector3(), toy3d::CameraProjectionMode::Perspective, 0.0f, 10.0f)),
                  "init_views must reject a non-positive near plane");
            check(invalid_view_rejected(toy3d::SceneView(
                      toy3d::Vector3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f), toy3d::Quaternion::identity(),
                      toy3d::Vector3(0.0f, 0.0f, 1.0f), toy3d::IntRect{0, 0, 128u, 128u}, toy3d::Extent{128u, 128u},
                      toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 10.0f)),
                  "init_views must reject non-finite camera values");
            check(invalid_view_rejected(toy3d::SceneView(
                      toy3d::Vector3(), toy3d::Quaternion::identity(), toy3d::Vector3(0.0f, 0.0f, 1.0f),
                      toy3d::IntRect{0, 0, 128u, 128u}, toy3d::Extent{64u, 128u},
                      toy3d::CameraProjectionMode::Perspective, toy3d::Radians(1.0f), 0.1f, 10.0f)),
                  "init_views must reject inconsistent output dimensions");
        }

        check(rendering_thread.stop().succeeded(), "CPU visibility fixture must stop the logical Rendering Thread");
        shutdown_graph(graph);
    }

    void test_canonical_uniform_parameter_values()
    {
        const toy3d::Matrix4 view_matrix(2.0f);
        const toy3d::Matrix4 projection_matrix(3.0f);
        const toy3d::Matrix4 view_projection_matrix(4.0f);
        const toy3d::Matrix4 inverse_view_matrix(5.0f);
        const toy3d::Matrix4 inverse_projection_matrix(6.0f);
        const toy3d::Matrix4 inverse_view_projection_matrix(7.0f);
        const toy3d::Vector3 camera_position(1.0f, 2.0f, 3.0f);
        const toy3d::Vector3 camera_direction(0.0f, 0.0f, 1.0f);
        const toy3d::ViewShaderParameters view_parameters{
            view_matrix,         projection_matrix,         view_projection_matrix,
            inverse_view_matrix, inverse_projection_matrix, inverse_view_projection_matrix,
            camera_position,     camera_direction};
        check(view_parameters.toy_view == view_matrix && view_parameters.toy_projection == projection_matrix &&
                  view_parameters.toy_view_projection == view_projection_matrix &&
                  view_parameters.toy_inverse_view == inverse_view_matrix &&
                  view_parameters.toy_inverse_projection == inverse_projection_matrix &&
                  view_parameters.toy_inverse_view_projection == inverse_view_projection_matrix &&
                  view_parameters.toy_camera_position == camera_position &&
                  view_parameters.toy_camera_direction == camera_direction,
              "Generated View parameters must preserve canonical matrices and camera values");

        toy3d::Matrix4 object_to_world = toy3d::Matrix4::identity();
        object_to_world.at(3, 0) = 2.0f;
        object_to_world.at(3, 1) = 3.0f;
        object_to_world.at(3, 2) = 4.0f;
        const toy3d::StaticMeshSceneProxy proxy(object_to_world, toy3d::AxisAlignedBounds{}, true, nullptr, {});
        check(proxy.object_shader_parameters().toy_object_to_world == object_to_world &&
                  proxy.object_data_generation() == 1u,
              "Generated Object parameters and their generation must initialize from copied Proxy values");
    }

    void test_renderer_bootstrap_failures()
    {
        const std::shared_ptr<const toy3d::GlobalShaderMap> global_shader_map = make_global_shader_map();
        const std::vector<RendererBootstrapFailurePoint> failure_points = {
            RendererBootstrapFailurePoint::DeviceFactory,
            RendererBootstrapFailurePoint::DeviceInitialize,
            RendererBootstrapFailurePoint::MissingGlobalShader,
            RendererBootstrapFailurePoint::ShaderProgram,
            RendererBootstrapFailurePoint::BindingLayout,
            RendererBootstrapFailurePoint::PassPipeline,
            RendererBootstrapFailurePoint::PlaceholderTexture,
            RendererBootstrapFailurePoint::PlaceholderView,
            RendererBootstrapFailurePoint::PlaceholderSampler,
            RendererBootstrapFailurePoint::CommandContext,
            RendererBootstrapFailurePoint::QueueSubmit,
            RendererBootstrapFailurePoint::QueueWait,
            RendererBootstrapFailurePoint::Viewport};

        for (const RendererBootstrapFailurePoint failure_point : failure_points)
        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, false);
            if (!graph)
            {
                return;
            }

            {
                toy3d::RHISurfaceDesc surface_desc;
                surface_desc.platform = toy3d::RHISurfacePlatform::Glfw;
                surface_desc.window_handle = reinterpret_cast<void*>(1);
                surface_desc.debug_name = "RendererBootstrapFailureSurface";
                toy3d::RHIViewportContextDesc viewport_desc;
                viewport_desc.extent = {64u, 64u};
                viewport_desc.debug_name = "RendererBootstrapFailureViewport";
                toy3d::Renderer renderer(
                    *graph, std::make_shared<toy3d::RHISurface>(std::move(surface_desc)), std::move(viewport_desc),
                    [failure_point]()
                    {
                        if (failure_point == RendererBootstrapFailurePoint::DeviceFactory)
                        {
                            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIDevice>>::failure(
                                toy3d::RHIErrorCode::BackendFailure,
                                "injected Renderer bootstrap device factory failure");
                        }
                        return toy3d::RHIResult<std::unique_ptr<toy3d::RHIDevice>>::success(
                            std::make_unique<RendererTestDevice>(failure_point));
                    },
                    failure_point == RendererBootstrapFailurePoint::MissingGlobalShader ? make_empty_global_shader_map()
                                                                                        : global_shader_map);
                toy3d::RenderingThread rendering_thread(thread_manager, *graph,
                                                        toy3d::RenderingThreadMode::SingleThread);
                const toy3d::ThreadStatus started = rendering_thread.start(
                    [&renderer]()
                    {
                        return renderer.initialize();
                    });
                const toy3d::RendererStatus renderer_status = renderer.status();
                const toy3d::RHIErrorCode expected_error_code =
                    failure_point == RendererBootstrapFailurePoint::MissingGlobalShader
                        ? toy3d::RHIErrorCode::InvalidArgument
                        : toy3d::RHIErrorCode::BackendFailure;
                check(!started.succeeded() && started.code == toy3d::ThreadErrorCode::InitFailed &&
                          !rendering_thread.is_ready() && renderer.scene_interface() == nullptr &&
                          renderer_status.lifecycle_state() == toy3d::RendererLifecycleState::Terminal &&
                          renderer_status.error_code() == expected_error_code &&
                          !renderer_status.error_message().empty(),
                      "Renderer bootstrap failure point " + std::to_string(static_cast<int>(failure_point)) +
                          " must preserve the original diagnostic and publish no SceneInterface; actual: " +
                          renderer_status.error_message());
            }

            shutdown_graph(graph);
        }
    }

    void test_renderer_imgui_bootstrap()
    {
        for (const bool include_imgui_shader : {false, true})
        {
            toy3d::ThreadManager thread_manager;
            std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, false);
            if (!graph)
            {
                return;
            }

            toy3d::RHISurfaceDesc surface_desc;
            surface_desc.platform = toy3d::RHISurfacePlatform::Glfw;
            surface_desc.window_handle = reinterpret_cast<void*>(1);
            surface_desc.debug_name = "RendererImGuiBootstrapSurface";
            toy3d::RHIViewportContextDesc viewport_desc;
            viewport_desc.extent = {64u, 64u};
            viewport_desc.debug_name = "RendererImGuiBootstrapViewport";
            auto font_atlas = std::make_unique<toy3d::ImGuiFontAtlasData>();
            font_atlas->rgba_pixels = {255u, 255u, 255u, 255u};
            font_atlas->width = 1u;
            font_atlas->height = 1u;
            font_atlas->row_pitch = 4u;

            {
                toy3d::Renderer renderer(
                    *graph, std::make_shared<toy3d::RHISurface>(std::move(surface_desc)), std::move(viewport_desc),
                    []()
                    {
                        return toy3d::RHIResult<std::unique_ptr<toy3d::RHIDevice>>::success(
                            std::make_unique<RendererTestDevice>());
                    },
                    make_global_shader_map(include_imgui_shader), std::move(font_atlas));
                toy3d::RenderingThread rendering_thread(thread_manager, *graph,
                                                        toy3d::RenderingThreadMode::SingleThread);
                const toy3d::ThreadStatus started = rendering_thread.start(
                    [&renderer]()
                    {
                        return renderer.initialize();
                    });
                if (!include_imgui_shader)
                {
                    check(!started.succeeded() && renderer.scene_interface() == nullptr &&
                              renderer.status().error_message().find("ImGuiGlobalShader") != std::string::npos,
                          "enabled ImGui must fail atomically when its Global Shader type is absent");
                }
                else
                {
                    check(started.succeeded() && renderer.scene_interface() != nullptr,
                          "enabled ImGui must publish only after Program and font bootstrap both complete; actual: " +
                              renderer.status().error_message());
                    const toy3d::ThreadStatus stopped = rendering_thread.stop(
                        [&renderer]()
                        {
                            return renderer.teardown();
                        });
                    check(stopped.succeeded(), "enabled ImGui Renderer domain must teardown cleanly");
                }
            }
            shutdown_graph(graph);
        }
    }

    void test_renderer_lifecycle(bool multithreaded, bool inject_terminal)
    {
        const std::shared_ptr<const toy3d::GlobalShaderMap> global_shader_map = make_global_shader_map();
        const std::shared_ptr<RendererDeviceProbe> device_probe = std::make_shared<RendererDeviceProbe>();
        toy3d::ThreadManager thread_manager;
        std::unique_ptr<toy3d::TaskGraphInterface> graph = create_graph(thread_manager, multithreaded);
        if (!graph)
        {
            return;
        }

        {
            toy3d::RHISurfaceDesc surface_desc;
            surface_desc.platform = toy3d::RHISurfacePlatform::Glfw;
            surface_desc.window_handle = reinterpret_cast<void*>(1);
            surface_desc.debug_name = "RendererLifecycleSurface";
            toy3d::RHIViewportContextDesc viewport_desc;
            viewport_desc.extent = {1280u, 720u};
            viewport_desc.debug_name = "RendererLifecycleViewport";
            toy3d::Renderer renderer(
                *graph, std::make_shared<toy3d::RHISurface>(std::move(surface_desc)), std::move(viewport_desc),
                [device_probe, inject_terminal]()
                {
                    return toy3d::RHIResult<std::unique_ptr<toy3d::RHIDevice>>::success(
                        std::make_unique<RendererTestDevice>(RendererBootstrapFailurePoint::None, device_probe,
                                                             inject_terminal));
                },
                global_shader_map, nullptr, false, {}, true);
            toy3d::Renderer* const stable_address = &renderer;
            {
                toy3d::RenderingThread rendering_thread(thread_manager, *graph,
                                                        multithreaded ? toy3d::RenderingThreadMode::MultiThread
                                                                      : toy3d::RenderingThreadMode::SingleThread);

                if (multithreaded)
                {
                    const toy3d::ThreadStatus wrong_thread = renderer.initialize();
                    check(wrong_thread.code == toy3d::ThreadErrorCode::InvalidCaller,
                          "GT must not initialize multi-thread Renderer mutable state");
                }

                const toy3d::ThreadStatus started = rendering_thread.start(
                    [&renderer, stable_address]()
                    {
                        check(&renderer == stable_address,
                              "Renderer address must remain stable during logical RT initialization");
                        const toy3d::ThreadStatus initialized = renderer.initialize();
                        check(initialized.succeeded(), "Renderer must initialize on the logical Rendering Thread");
                        check(renderer.initialize().code == toy3d::ThreadErrorCode::InvalidState,
                              "Renderer must reject repeated initialization");
                        return initialized;
                    });
                check(started.succeeded(), "RenderingThread bootstrap must initialize Renderer");
                check(renderer.status().lifecycle_state() == toy3d::RendererLifecycleState::Running &&
                          !renderer.status().has_error(),
                      "Renderer bootstrap must publish Running only after the complete domain succeeds");

                toy3d::SceneInterface* const scene_interface = renderer.scene_interface();
                check(scene_interface != nullptr,
                      "Renderer must publish its stable SceneInterface after initialization");
                toy3d::SceneInterface* const play_interface = renderer.play_scene_interface();
                check(play_interface && play_interface != scene_interface,
                      "Play must publish an independent Renderer-owned scene");

                toy3d::Vector3 camera_position(1.0f, 2.0f, 3.0f);
                toy3d::Quaternion camera_orientation = toy3d::Quaternion::identity();
                toy3d::Vector3 camera_direction(0.0f, 0.0f, 1.0f);
                toy3d::Radians vertical_fov(1.0f);
                std::vector<toy3d::SceneView> views;
                views.emplace_back(camera_position, camera_orientation, camera_direction,
                                   toy3d::IntRect{10, 20, 640u, 360u}, toy3d::Extent{1280u, 720u},
                                   toy3d::CameraProjectionMode::Perspective, vertical_fov, 0.25f, 500.0f);
                toy3d::SceneViewFamily view_family(*scene_interface, toy3d::Extent{1280u, 720u}, std::move(views));
                camera_position.x = 99.0f;
                camera_orientation = toy3d::Quaternion(0.0f, 0.0f, 0.0f, 0.0f);
                camera_direction.z = -1.0f;
                vertical_fov = toy3d::Radians(2.0f);
                check(&view_family.scene_interface() == scene_interface &&
                          view_family.output_extent() == toy3d::Extent{1280u, 720u} &&
                          view_family.views().size() == 1 &&
                          view_family.views()[0].camera_position() == toy3d::Vector3(1.0f, 2.0f, 3.0f) &&
                          view_family.views()[0].camera_orientation() == toy3d::Quaternion::identity() &&
                          view_family.views()[0].camera_direction() == toy3d::Vector3(0.0f, 0.0f, 1.0f) &&
                          view_family.views()[0].view_rect() == toy3d::IntRect{10, 20, 640u, 360u} &&
                          view_family.views()[0].output_extent() == toy3d::Extent{1280u, 720u} &&
                          view_family.views()[0].projection_mode() == toy3d::CameraProjectionMode::Perspective &&
                          view_family.views()[0].vertical_fov() == toy3d::Radians(1.0f) &&
                          view_family.views()[0].near_clip() == 0.25f && view_family.views()[0].far_clip() == 500.0f &&
                          !view_family.views()[0].infinite_far(),
                      "SceneViewFamily must own copied Camera, viewport, and projection values");
                // The lifecycle fake deliberately does not acquire a presentation frame;
                // Batch D owns the complete Forward Base Pass frame matrix.
                toy3d::World world;
                toy3d::StaticMeshActor& actor = world.spawn_actor<toy3d::StaticMeshActor>();
                toy3d::MaterialInstanceRef material;
                toy3d::StaticMeshRef mesh = make_mesh(&material);
                actor.static_mesh_component().set_static_mesh(mesh);
                check(world.scene_interface() == nullptr, "World must begin without a scene binding");
                check(world.bind_scene(*scene_interface) && world.scene_interface() == scene_interface,
                      "World must bind the Renderer-owned SceneInterface non-owningly");
                check(actor.static_mesh_component().has_render_state(),
                      "bind_scene must create render state for an existing registered Primitive");
                check(!world.bind_scene(*scene_interface), "World must reject a second scene binding");
                toy3d::Transform moved_transform;
                moved_transform.translation = {2.0f, 3.0f, 4.0f};
                check(actor.static_mesh_component().set_local_transform(moved_transform),
                      "Primitive transform changes must produce an owned-value scene update");
                for (int iteration = 0; iteration != 2; ++iteration)
                {
                    toy3d::World play_world;
                    auto& play_actor = play_world.spawn_actor<toy3d::StaticMeshActor>();
                    toy3d::MaterialInstanceRef play_material;
                    auto play_mesh = make_mesh(&play_material);
                    play_actor.static_mesh_component().set_static_mesh(play_mesh);
                    check(play_world.bind_scene(*play_interface), "Fresh Play World binds independently");
                    check(play_world.unbind_scene() && toy3d::flush_rendering_commands().succeeded(),
                          "Play removal drains without withdrawing the author scene");
                    check(world.scene_interface() == scene_interface &&
                              actor.static_mesh_component().has_render_state() &&
                              actor.static_mesh_component().static_mesh() == mesh,
                          "Repeated Play keeps author registration and resource identity intact");
                    play_actor.static_mesh_component().set_static_mesh(nullptr);
                    play_mesh.reset();
                    toy3d::MaterialInstance::release(play_material);
                    check(toy3d::flush_rendering_commands().succeeded(), "Play material release drains");
                }
                check(world.unbind_scene() && world.scene_interface() == nullptr,
                      "World must clear its non-owning scene binding before Renderer teardown");
                check(!actor.static_mesh_component().has_render_state(),
                      "unbind_scene must destroy Primitive render state before clearing the scene");
                check(!world.unbind_scene(), "World must diagnose unbind without an active scene binding");
                const toy3d::RenderFenceWaitResult drained = toy3d::flush_rendering_commands();
                check(drained.succeeded(), "Scene add/update/remove ownership must drain before Renderer teardown");
                actor.static_mesh_component().set_static_mesh(nullptr);
                mesh.reset();
                try
                {
                    toy3d::MaterialInstance::release(material);
                }
                catch (const std::exception& exception)
                {
                    check(false, std::string("Material proxy release must be admissible before Renderer teardown: ") +
                                     exception.what());
                }
                const toy3d::RenderFenceWaitResult material_released = toy3d::flush_rendering_commands();
                check(material_released.succeeded(), "MaterialRenderProxy release must drain before Renderer teardown");

                if (!inject_terminal)
                {
                    for (const auto code : {toy3d::RHIErrorCode::InvalidArgument, toy3d::RHIErrorCode::Unsupported})
                    {
                        device_probe->frame_error.store(code);
                        toy3d::ViewportFrameOutput rejected_output;
                        rejected_output.play_scene = true;
                        rejected_output.scene_feedback = std::make_shared<toy3d::SceneRenderFeedback>();
                        std::vector<toy3d::SceneView> rejected_views;
                        rejected_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(),
                                                    toy3d::Vector3(0, 0, 1), toy3d::IntRect{0, 0, 1u, 1u},
                                                    toy3d::Extent{1u, 1u}, toy3d::CameraProjectionMode::Perspective,
                                                    toy3d::Radians(1.0f), 0.1f, 100.0f);
                        renderer.draw_frame(std::make_unique<toy3d::ForwardSceneRenderer>(toy3d::SceneViewFamily(
                                                *play_interface, toy3d::Extent{1u, 1u}, std::move(rejected_views))),
                                            nullptr, rejected_output);
                        check(toy3d::flush_rendering_commands().succeeded() &&
                                  rejected_output.scene_feedback->state.load(std::memory_order_acquire) ==
                                      toy3d::SceneRenderState::Failed &&
                                  renderer.status().lifecycle_state() == toy3d::RendererLifecycleState::Running,
                              "Rejected Play input publishes failure without terminating author domain");
                    }
                }

                if (inject_terminal)
                {
                    toy3d::World terminal_world;
                    toy3d::StaticMeshActor& terminal_actor = terminal_world.spawn_actor<toy3d::StaticMeshActor>();
                    toy3d::MaterialInstanceRef terminal_material;
                    toy3d::StaticMeshRef terminal_mesh = make_mesh(&terminal_material);
                    terminal_actor.static_mesh_component().set_static_mesh(terminal_mesh);
                    std::vector<toy3d::SceneView> terminal_views;
                    terminal_views.emplace_back(toy3d::Vector3(), toy3d::Quaternion::identity(),
                                                toy3d::Vector3(0.0f, 0.0f, 1.0f), toy3d::IntRect{0, 0, 1u, 1u},
                                                toy3d::Extent{1u, 1u}, toy3d::CameraProjectionMode::Perspective,
                                                toy3d::Radians(1.0f), 0.1f, 100.0f);
                    toy3d::ViewportFrameOutput terminal_output;
                    terminal_output.play_scene = true;
                    terminal_output.scene_feedback = std::make_shared<toy3d::SceneRenderFeedback>();
                    renderer.draw_frame(std::make_unique<toy3d::ForwardSceneRenderer>(toy3d::SceneViewFamily(
                                            *play_interface, toy3d::Extent{1u, 1u}, std::move(terminal_views))),
                                        nullptr, terminal_output);
                    check(terminal_world.bind_scene(*scene_interface),
                          "Terminal admission window must still accept Add ownership");
                    check(terminal_actor.static_mesh_component().has_render_state(),
                          "Normal Add return must publish only opaque identity during terminal disposal");
                    check(terminal_world.unbind_scene(),
                          "Terminal admission window must accept Remove disposal ordering");
                    check(!terminal_actor.static_mesh_component().has_render_state(),
                          "Terminal Remove must clear opaque identity without dereferencing it");
                    const toy3d::RenderFenceWaitResult terminal_drained = toy3d::flush_rendering_commands();
                    check(terminal_drained.succeeded() &&
                              renderer.status().lifecycle_state() == toy3d::RendererLifecycleState::Terminal &&
                              renderer.status().error_code() == toy3d::RHIErrorCode::DeviceLost,
                          "Terminal Draw must latch first error and drain skipped ownership payloads");
                    terminal_actor.static_mesh_component().set_static_mesh(nullptr);
                    terminal_mesh.reset();
                    try
                    {
                        toy3d::MaterialInstance::release(terminal_material);
                    }
                    catch (const std::exception& exception)
                    {
                        check(false,
                              std::string("Terminal Material proxy release must be admissible: ") + exception.what());
                    }
                }

                const toy3d::ThreadStatus stopped = rendering_thread.stop(
                    [&renderer, stable_address]()
                    {
                        check(&renderer == stable_address,
                              "Renderer address must remain stable during logical RT teardown");
                        const toy3d::ThreadStatus torn_down = renderer.teardown();
                        check(torn_down.succeeded(), "Renderer must teardown on the logical Rendering Thread");
                        check(renderer.teardown().code == toy3d::ThreadErrorCode::InvalidState,
                              "Renderer must reject repeated teardown");
                        return torn_down;
                    });
                check(stopped.succeeded(), "RenderingThread stop must teardown Renderer before returning");
                check(global_shader_map.use_count() == 1, "Renderer must release its GlobalShaderMap reference during "
                                                          "logical RT teardown before Engine-owner release");
                check(renderer.scene_interface() == nullptr && renderer.play_scene_interface() == nullptr,
                      "Renderer must withdraw SceneInterface publication after teardown");
                check(renderer.status().lifecycle_state() == (inject_terminal ? toy3d::RendererLifecycleState::Terminal
                                                                              : toy3d::RendererLifecycleState::Stopped),
                      "Renderer teardown must preserve sticky Terminal or publish normal Stopped");
                check(&renderer == stable_address,
                      "GT-owned Renderer shell address must remain stable for its full lifetime");
                check(device_probe->shutdown_count == 1u,
                      "Renderer teardown must issue exactly one bounded device shutdown");
#if !defined(NDEBUG)
                check(device_probe->validation_enabled,
                      "Debug Renderer bootstrap must enable the backend-independent RHI validation contract");
#endif
                if (inject_terminal)
                {
                    check(device_probe->wait_idle_before_shutdown_count == 0u,
                          "DeviceLost teardown must not enter the device wait-idle path");
                    check(renderer.status().error_code() == toy3d::RHIErrorCode::DeviceLost &&
                              renderer.status().secondary_diagnostic().find("secondary terminal shutdown") !=
                                  std::string::npos,
                          "secondary cleanup failure must not replace the first DeviceLost error");
                }
            }
        }

        shutdown_graph(graph);
    }
} // namespace

int main()
{
    test_canonical_uniform_parameter_values();
    test_cpu_view_and_visibility();
    test_renderer_bootstrap_failures();
    test_renderer_imgui_bootstrap();
    test_renderer_lifecycle(true, false);
    test_renderer_lifecycle(false, false);
    test_renderer_lifecycle(false, true);
    test_renderer_lifecycle(true, true);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " Renderer scene ownership test(s) failed\n";
        return 1;
    }
    std::cout << "All Renderer scene ownership tests passed\n";
    return 0;
}
