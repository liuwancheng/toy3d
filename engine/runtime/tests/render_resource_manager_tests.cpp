#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_queue.h"
#include "rendercore/material/material.h"
#include "rendercore/rendering_thread.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/shader/primitive_uniform_shader_parameters.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "rendercore/texture/texture.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/render_scene.h"
#include "renderscene/render_resource.h"
#include "renderscene/render_resource_manager.h"
#include "renderscene/texture/texture_resource.h"
#include "renderscene/view/forward_scene_renderer.h"
#include "task_graph/task_graph.h"
#include "threading/thread_manager.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
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

    toy3d::ShaderMapProgramData make_material_program(
        const std::string& pass_name,
        std::uint8_t hash_seed)
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/Test/Material";
        program.pass_name = pass_name;
        program.mapping_version = 1;
        program.logical_layout_hash = nonzero_hash(hash_seed);
        program.target_binding_hash = nonzero_hash(hash_seed + 1u);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(
                program.graphics_pass_state);
        program.permutation_key = nonzero_hash(hash_seed + 3u);

        toy3d::ShaderMapBinding constants;
        constants.parameter_id = 10u;
        constants.name = "MaterialConstants";
        constants.group = toy3d::RHIBindingGroup::Material;
        constants.type = toy3d::RHIResourceBindingType::UniformBuffer;
        constants.stages = toy3d::RHIShaderStageFlags::Vertex;
        constants.target_binding = 0u;
        constants.constant_buffer_size = 32u;
        constants.constant_members.push_back({11u, "roughness",
            toy3d::ShaderValueType::Float32, 0u, 4u, 0u, 0u});
        constants.constant_members.push_back({13u, "base_color",
            toy3d::ShaderValueType::Float32x4, 16u, 16u, 0u, 0u});
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
        return program;
    }

    class MaterialProgramLoader final : public toy3d::ShaderMapLoader
    {
    public:
        explicit MaterialProgramLoader(toy3d::ShaderMapProgramData program)
            : program_(std::move(program))
        {
        }

        toy3d::ShaderMapProgramLoadResult load_program(
            const toy3d::ShaderMapProgramKey&) const override
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
        program.mapping_version = 1;
        program.logical_layout_hash = nonzero_hash(60u);
        program.target_binding_hash = nonzero_hash(61u);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(
                program.graphics_pass_state);
        program.permutation_key = nonzero_hash(62u);

        toy3d::ShaderMapBinding view;
        view.parameter_id = 100u;
        view.name = "toy_view_data";
        view.group = toy3d::RHIBindingGroup::View;
        view.type = toy3d::RHIResourceBindingType::UniformBuffer;
        view.stages = toy3d::RHIShaderStageFlags::Vertex;
        view.target_binding = 0u;
        view.constant_buffer_size = 416u;
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_view"), "toy_view",
            toy3d::ShaderValueType::Float32x4x4, 0u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_projection"), "toy_projection",
            toy3d::ShaderValueType::Float32x4x4, 64u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_view_projection"), "toy_view_projection",
            toy3d::ShaderValueType::Float32x4x4, 128u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_inverse_view"), "toy_inverse_view",
            toy3d::ShaderValueType::Float32x4x4, 192u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_inverse_projection"), "toy_inverse_projection",
            toy3d::ShaderValueType::Float32x4x4, 256u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_inverse_view_projection"), "toy_inverse_view_projection",
            toy3d::ShaderValueType::Float32x4x4, 320u, 64u, 0u, 16u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_camera_position"), "toy_camera_position",
            toy3d::ShaderValueType::Float32x3, 384u, 12u, 0u, 0u});
        view.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_camera_direction"), "toy_camera_direction",
            toy3d::ShaderValueType::Float32x3, 400u, 12u, 0u, 0u});
        program.bindings.push_back(view);

        toy3d::ShaderMapBinding object;
        object.parameter_id = 110u;
        object.name = "toy_object_data";
        object.group = toy3d::RHIBindingGroup::Object;
        object.type = toy3d::RHIResourceBindingType::UniformBuffer;
        object.stages = toy3d::RHIShaderStageFlags::Vertex;
        object.target_binding = 0u;
        object.constant_buffer_size = 64u;
        object.constant_members.push_back({toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::Object,
            toy3d::shader::ShaderParameterCategory::Constant, "toy_object_to_world"), "toy_object_to_world",
            toy3d::ShaderValueType::Float32x4x4, 0u, 64u, 0u, 16u});
        program.bindings.push_back(object);

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {1u, 2u, 3u, 60u};
        vertex.content_hash = nonzero_hash(63u);
        vertex.reflection = program.bindings;
        program.stages.push_back(std::move(vertex));
        return program;
    }

    toy3d::ShaderMapProgramRef load_program(
        toy3d::ShaderMapProgramData program)
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
}

int main()
{
    struct : toy3d::RHIQueue
    {
        toy3d::RHIQueueCompletionValue completed_value() const override
        {
            return 0;
        }

        toy3d::RHIStatus wait_for_value(
            toy3d::RHIQueueCompletionValue) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus wait_idle() override
        {
            return toy3d::RHIStatus::success();
        }

    protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(
            const toy3d::RHISubmitInfo&) override
        {
            return toy3d::RHIResult<toy3d::RHISubmitResult>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke does not submit command lists");
        }
    } queue;

    struct : toy3d::RHIDevice
    {
        toy3d::RHIQueue* queue = nullptr;
        toy3d::RHICapabilities test_capabilities;
        toy3d::RHILimits test_limits;
        toy3d::RHITextureDesc last_texture_desc;
        std::vector<std::uint8_t> last_buffer_initial_data;
        std::uint32_t buffer_creation_count = 0;
        std::uint32_t binding_set_creation_count = 0;

        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        const toy3d::RHICapabilities& capabilities() const override
        {
            return test_capabilities;
        }

        const toy3d::RHILimits& limits() const override
        {
            return test_limits;
        }

        toy3d::RHIFormatCapabilities format_capabilities(
            toy3d::PixelFormat) const override
        {
            return {};
        }

        toy3d::RHIQueue& graphics_queue() override
        {
            return *queue;
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>
        create_viewport_context(
            const toy3d::RHISurfaceRef&,
            const toy3d::RHIViewportContextDesc&) override
        {
            return toy3d::RHIResult<
                std::unique_ptr<toy3d::RHIViewportContext>>::failure(
                    toy3d::RHIErrorCode::Unsupported,
                    "The resource smoke has no viewport");
        }

        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer(
            const toy3d::RHIBufferDesc& desc,
            const toy3d::RHIInitialData* initial_data) override
        {
            ++buffer_creation_count;
            last_buffer_initial_data.clear();
            if (initial_data != nullptr && initial_data->data != nullptr)
            {
                const auto* begin = static_cast<const std::uint8_t*>(
                    initial_data->data);
                last_buffer_initial_data.assign(
                    begin, begin + initial_data->size);
            }
            return toy3d::RHIResult<toy3d::RHIBufferRef>::success(
                std::make_shared<toy3d::RHIBuffer>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHITextureRef> create_texture(
            const toy3d::RHITextureDesc& desc,
            const toy3d::RHIInitialData*) override
        {
            last_texture_desc = desc;
            return toy3d::RHIResult<toy3d::RHITextureRef>::success(
                std::make_shared<toy3d::RHITexture>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view(
            const toy3d::RHIBufferRef&,
            const toy3d::RHIBufferViewDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIBufferViewRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke creates no views");
        }

        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view(
            const toy3d::RHITextureRef& texture,
            const toy3d::RHITextureViewDesc& desc) override
        {
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                std::make_shared<toy3d::RHITextureView>(texture, desc));
        }

        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler(
            const toy3d::RHISamplerDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHISamplerRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke creates no samplers");
        }

        toy3d::RHIResult<toy3d::RHIBindingSetRef> create_binding_set(
            const toy3d::RHIBindingSetDesc& desc) override
        {
            ++binding_set_creation_count;
            return toy3d::RHIResult<toy3d::RHIBindingSetRef>::success(
                std::make_shared<toy3d::RHIBindingSet>(desc));
        }

        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence(
            const std::string&) override
        {
            return toy3d::RHIResult<toy3d::RHIGPUFenceRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke creates no fences");
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>
        create_graphics_command_context() override
        {
            return toy3d::RHIResult<
                std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::failure(
                    toy3d::RHIErrorCode::Unsupported,
                    "The resource smoke injects its command context");
        }

    protected:
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(
            const toy3d::RHIShaderDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIShaderRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke creates no shaders");
        }

        toy3d::RHIResult<toy3d::RHIBindingLayoutRef>
        create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke creates no binding layouts");
        }

        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>
        create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The resource smoke creates no pipelines");
        }

        bool is_initialized_impl() const override
        {
            return true;
        }

        toy3d::RHIStatus wait_idle_before_shutdown_impl() override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus shutdown_impl() override
        {
            return toy3d::RHIStatus::success();
        }
    } device;
    device.queue = &queue;

    struct : toy3d::RHICommandContext
    {
        std::vector<std::uint8_t> last_buffer_upload_data;

        toy3d::RHIStatus begin_recording(const std::string&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus transition_resources(
            const std::vector<toy3d::RHIResourceTransition>&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_buffer(
            const toy3d::RHIBufferCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_buffer(
            const toy3d::RHIBufferUploadDesc& desc) override
        {
            const auto* begin = static_cast<const std::uint8_t*>(
                desc.source.data);
            last_buffer_upload_data.assign(
                begin, begin + desc.source.size);
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_texture(
            const toy3d::RHITextureCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_texture(
            const toy3d::RHITextureUploadDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus write_gpu_fence(
            const toy3d::RHIGPUFenceRef&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIResult<toy3d::RHICommandListRef>
        finish_recording() override
        {
            return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The ABI materialization smoke does not finish a command list");
        }
    } uniform_context;

    toy3d::RHIBindingLayoutDesc view_object_layout_desc;
    view_object_layout_desc.entries.push_back({toy3d::RHIBindingGroup::View,
        0u, toy3d::RHIResourceBindingType::UniformBuffer,
        toy3d::RHIShaderStageFlags::Vertex, 1u});
    view_object_layout_desc.entries.push_back({toy3d::RHIBindingGroup::Object,
        0u, toy3d::RHIResourceBindingType::UniformBuffer,
        toy3d::RHIShaderStageFlags::Vertex, 1u});
    const toy3d::RHIBindingLayoutRef view_object_layout =
        std::make_shared<toy3d::RHIBindingLayout>(
            std::move(view_object_layout_desc));
    const toy3d::ShaderMapProgramRef view_object_program =
        load_program(make_view_object_program());

    const toy3d::ViewUniformShaderParameters view_parameters{
        toy3d::Matrix4(1.0f),
        toy3d::Matrix4(2.0f),
        toy3d::Matrix4(3.0f),
        toy3d::Matrix4(4.0f),
        toy3d::Matrix4(5.0f),
        toy3d::Matrix4(6.0f),
        toy3d::Vector3(7.0f, 8.0f, 9.0f),
        0.0f,
        toy3d::Vector3(10.0f, 11.0f, 12.0f),
        0.0f};
    const auto view_binding =
        toy3d::materialize_view_uniform_shader_parameters(
            device, uniform_context, view_object_layout, *view_object_program,
            view_parameters);
    float view_projection_diagonal = 0.0f;
    float camera_position_x = 0.0f;
    const bool view_bytes_complete =
        uniform_context.last_buffer_upload_data.size() == 416u;
    if (view_bytes_complete)
    {
        std::memcpy(&view_projection_diagonal,
            uniform_context.last_buffer_upload_data.data() + 128u,
            sizeof(float));
        std::memcpy(&camera_position_x,
            uniform_context.last_buffer_upload_data.data() + 384u,
            sizeof(float));
    }
    check(view_binding.succeeded() && view_binding.value() != nullptr &&
          view_binding.value()->group() == toy3d::RHIBindingGroup::View &&
          view_bytes_complete &&
          view_projection_diagonal == 3.0f && camera_position_x == 7.0f &&
          uniform_context.last_buffer_upload_data[396u] == 0u,
        "View ABI materialization must write canonical members and keep padding zero");

    toy3d::Matrix4 object_to_world = toy3d::Matrix4::identity();
    object_to_world.at(3u, 0u) = 13.0f;
    const toy3d::PrimitiveUniformShaderParameters object_parameters{
        object_to_world};
    const auto object_binding =
        toy3d::materialize_primitive_uniform_shader_parameters(
            device, uniform_context, view_object_layout, *view_object_program,
            object_parameters);
    float object_translation_x = 0.0f;
    const bool object_bytes_complete =
        uniform_context.last_buffer_upload_data.size() == 64u;
    if (object_bytes_complete)
    {
        std::memcpy(&object_translation_x,
            uniform_context.last_buffer_upload_data.data() + 48u,
            sizeof(float));
    }
    check(object_binding.succeeded() && object_binding.value() != nullptr &&
          object_binding.value()->group() == toy3d::RHIBindingGroup::Object &&
          object_bytes_complete &&
          object_translation_x == 13.0f,
        "Object ABI materialization must write the canonical object matrix");

    const toy3d::ShaderMapProgramRef material_only_program =
        load_program(make_material_program("UnusedView", 70u));
    const std::uint32_t unused_buffer_count = device.buffer_creation_count;
    const std::uint32_t unused_set_count = device.binding_set_creation_count;
    const auto unused_view =
        toy3d::materialize_view_uniform_shader_parameters(
            device, uniform_context, view_object_layout, *material_only_program,
            view_parameters);
    check(unused_view.succeeded() && unused_view.value() == nullptr &&
          device.buffer_creation_count == unused_buffer_count &&
          device.binding_set_creation_count == unused_set_count,
        "an unused View group must not create a buffer or binding set");

    toy3d::ShaderMapProgramData invalid_view_data =
        make_view_object_program();
    invalid_view_data.bindings[0].constant_members[0].name =
        "toy_unknown_view_member";
    invalid_view_data.stages[0].reflection = invalid_view_data.bindings;
    const toy3d::ShaderMapProgramRef invalid_view_program =
        load_program(std::move(invalid_view_data));
    check(!toy3d::materialize_view_uniform_shader_parameters(
               device, uniform_context, view_object_layout,
               *invalid_view_program,
               view_parameters),
        "unknown View members must fail with a diagnostic");

    toy3d::ShaderMapProgramData mismatched_identity_data =
        make_view_object_program();
    mismatched_identity_data.bindings[0].constant_members[0].parameter_id =
        toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant,
            "toy_projection");
    mismatched_identity_data.bindings[0].constant_members[1].parameter_id =
        toy3d::shader::make_shader_parameter_id(
            toy3d::shader::BindingGroup::View,
            toy3d::shader::ShaderParameterCategory::Constant,
            "toy_view");
    mismatched_identity_data.stages[0].reflection =
        mismatched_identity_data.bindings;
    const toy3d::ShaderMapProgramRef mismatched_identity_program =
        load_program(std::move(mismatched_identity_data));
    check(!toy3d::materialize_view_uniform_shader_parameters(
               device, uniform_context, view_object_layout,
               *mismatched_identity_program,
               view_parameters),
        "View member names with mismatched stable identities must fail");

    toy3d::ShaderMapProgramData invalid_stride_data =
        make_view_object_program();
    invalid_stride_data.bindings[1].constant_members[0].matrix_stride = 12u;
    invalid_stride_data.stages[0].reflection = invalid_stride_data.bindings;
    const toy3d::ShaderMapProgramRef invalid_stride_program =
        load_program(std::move(invalid_stride_data));
    check(!toy3d::materialize_primitive_uniform_shader_parameters(
               device, uniform_context, view_object_layout,
               *invalid_stride_program,
               object_parameters),
        "incompatible Object matrix stride must fail before RHI creation");

    toy3d::ShaderMapProgramData resource_view_data =
        make_view_object_program();
    resource_view_data.bindings[0].type =
        toy3d::RHIResourceBindingType::SampledTexture;
    resource_view_data.bindings[0].constant_buffer_size = 0u;
    resource_view_data.bindings[0].constant_members.clear();
    resource_view_data.stages[0].reflection = resource_view_data.bindings;
    const toy3d::ShaderMapProgramRef resource_view_program =
        load_program(std::move(resource_view_data));
    check(!toy3d::materialize_view_uniform_shader_parameters(
               device, uniform_context, view_object_layout,
               *resource_view_program,
               view_parameters),
        "View resource-class bindings without a canonical source must fail");

    struct : toy3d::RHIGraphicsCommandContext
    {
        std::uint32_t transition_count = 0;
        std::uint32_t upload_count = 0;
        std::uint32_t texture_upload_count = 0;
        std::vector<std::uint8_t> last_buffer_upload_data;
        std::vector<std::string>* operations = nullptr;
        toy3d::RHIDevice* command_device = nullptr;
        bool finish_success = false;

        toy3d::RHIStatus begin_recording(const std::string&) override
        {
            if (operations != nullptr)
            {
                operations->push_back("begin_recording");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus transition_resources(
            const std::vector<toy3d::RHIResourceTransition>& transitions) override
        {
            transition_count += static_cast<std::uint32_t>(transitions.size());
            if (operations != nullptr)
            {
                operations->push_back("transition");
            }
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_buffer(
            const toy3d::RHIBufferCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_buffer(
            const toy3d::RHIBufferUploadDesc& desc) override
        {
            ++upload_count;
            const auto* begin = static_cast<const std::uint8_t*>(
                desc.source.data);
            last_buffer_upload_data.assign(
                begin, begin + desc.source.size);
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus copy_texture(
            const toy3d::RHITextureCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus upload_texture(
            const toy3d::RHITextureUploadDesc&) override
        {
            ++texture_upload_count;
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus write_gpu_fence(
            const toy3d::RHIGPUFenceRef&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIResult<toy3d::RHICommandListRef>
        finish_recording() override
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
                toy3d::RHIErrorCode::Unsupported,
                "The manager smoke does not finish an RHI command list");
        }

        toy3d::RHIStatus begin_render_pass(
            const toy3d::RHIRenderPassDesc&) override
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

        toy3d::RHIStatus set_graphics_pipeline(
            const toy3d::RHIGraphicsPipelineRef&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_viewport(
            const toy3d::RHIViewport&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_scissor(const toy3d::RHIRect&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_blend_constants(
            const toy3d::vec4&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_stencil_reference(std::uint8_t) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_vertex_buffers(
            const std::vector<toy3d::RHIVertexBufferBinding>&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus set_index_buffer(
            const toy3d::RHIIndexBufferBinding&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus draw(const toy3d::RHIDrawArgs&) override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus draw_indexed(
            const toy3d::RHIDrawIndexedArgs&) override
        {
            return toy3d::RHIStatus::success();
        }

    protected:
        toy3d::RHIStatus bind_graphics_bindings_impl(
            const toy3d::RHIGraphicsBindings&) override
        {
            return toy3d::RHIStatus::success();
        }
    } context;

    struct : toy3d::RenderResource
    {
        int record_count = 0;
        int commit_count = 0;
        int discard_count = 0;
        int release_count = 0;
        bool retryable_failure = false;
        bool deterministic_failure = false;
        std::vector<std::string>* operations = nullptr;

        toy3d::RHIStatus record_upload(
            toy3d::RHIDevice&,
            toy3d::RHIGraphicsCommandContext&) override
        {
            ++record_count;
            if (operations != nullptr)
            {
                operations->push_back("record_pending_uploads");
            }
            if (deterministic_failure)
            {
                return fail(toy3d::RHIStatus::failure(
                    toy3d::RHIErrorCode::Unsupported,
                    "Deterministic test resource failure"));
            }
            if (retryable_failure)
            {
                return toy3d::RHIStatus::failure(
                    toy3d::RHIErrorCode::BackendFailure,
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

        void release_rhi() noexcept override
        {
            ++release_count;
        }
    } ready_first, ready_second, retry_first, retry_second,
        deterministic, released_while_recording, terminal_first, terminal_second;

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
          ready_second.state() == toy3d::RenderResourceState::Ready &&
          ready_first.commit_count == 1 && ready_second.commit_count == 1,
        "commit must atomically publish all recorded resources Ready");
    check(manager.release(ready_first) && manager.release(ready_second),
        "Ready resources must release RHI refs without waiting for GPU completion");

    retry_second.retryable_failure = true;
    check(manager.begin_init(retry_first) && manager.begin_init(retry_second),
        "retry resources must enter PendingUpload");
    const toy3d::RHIStatus recording_failure =
        manager.record_pending_uploads(context);
    check(!recording_failure &&
          retry_first.state() == toy3d::RenderResourceState::PendingUpload &&
          retry_second.state() == toy3d::RenderResourceState::PendingUpload,
        "a retryable recording failure must keep every resource PendingUpload");
    check(!manager.commit_recording(),
        "a failed recording must not be commit eligible");
    check(manager.discard_recording() && retry_first.discard_count == 1 &&
          retry_second.discard_count == 1,
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
    const toy3d::MaterialInstanceRef material_instance =
        toy3d::MaterialInstance::create(material);
    toy3d::StaticMeshDesc mesh_desc;
    mesh_desc.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
    mesh_desc.vertex_colors = {
        {255u, 0u, 0u, 255u},
        {0u, 255u, 0u, 255u},
        {0u, 0u, 255u, 255u}};
    // The mesh owns one explicit index width; UInt16 is sufficient for this
    // triangle and lets the smoke verify the matching RHI binding format.
    mesh_desc.indices = std::vector<std::uint16_t>{0u, 1u, 2u};
    mesh_desc.sections.push_back({0u, 3u, 0u});
    mesh_desc.material_slots.push_back(material_instance);
    const toy3d::StaticMeshRef static_mesh =
        toy3d::StaticMesh::create(std::move(mesh_desc));
    check(static_mesh != nullptr,
        "the StaticMesh resource smoke requires a valid immutable Asset");

    toy3d::StaticMeshRenderData render_data(*static_mesh);
    check(render_data.begin_init(manager).succeeded(),
        "StaticMeshRenderData begin_init must enqueue its complete buffer candidate");
    const std::uint32_t transitions_before_mesh = context.transition_count;
    const std::uint32_t uploads_before_mesh = context.upload_count;
    check(manager.record_pending_uploads(context).succeeded() &&
          render_data.prepare_current_recording().succeeded() &&
          render_data.is_drawable(),
        "all mesh uploads and LocalVertexFactory validation must open the current-list candidate gate");
    check(context.transition_count - transitions_before_mesh == 8u &&
          context.upload_count - uploads_before_mesh == 4u,
        "four mesh buffers must each record two transitions and one upload");
    check(manager.discard_recording().succeeded() &&
          !render_data.is_drawable(),
        "discard must close the complete mesh gate and retain payload for retry");

    check(manager.record_pending_uploads(context).succeeded() &&
          render_data.prepare_current_recording().succeeded() &&
          manager.commit_recording().succeeded() &&
          render_data.is_drawable(),
        "a later valid submit commit must publish every mesh buffer Ready together");
    const toy3d::RHIIndexBufferBinding index_binding =
        render_data.index_buffer_binding();
    check(index_binding.buffer != nullptr &&
          index_binding.format == toy3d::RHIIndexFormat::UInt16,
        "StaticMeshIndexBuffer must retain the Asset index-width contract");
    toy3d::MaterialRenderProxy batch_material_proxy(*material);
    toy3d::StaticMeshSceneProxy batch_scene_proxy(
        toy3d::Matrix4::identity(),
        static_mesh->local_bounds(),
        true,
        &render_data,
        {&batch_material_proxy});
    const toy3d::StaticMeshSection& batch_section =
        render_data.sections().front();
    toy3d::MeshBatch mesh_batch(
        batch_scene_proxy,
        render_data,
        *render_data.vertex_factory(),
        batch_material_proxy,
        batch_section.first_index,
        batch_section.index_count);
    check(&mesh_batch.scene_proxy() == &batch_scene_proxy &&
          &mesh_batch.render_data() == &render_data &&
          &mesh_batch.vertex_factory() == render_data.vertex_factory() &&
          &mesh_batch.material_render_proxy() == &batch_material_proxy &&
          mesh_batch.first_index() == 0u &&
          mesh_batch.index_count() == 3u,
        "MeshBatch must compose one frame-local section with non-owning Proxy, RenderData, LocalVertexFactory, and MaterialRenderProxy references");

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
          render_data.vertex_factory()->build_vertex_input(
              shader_inputs, layouts, attributes, bindings).succeeded() &&
          layouts.size() == 3u && attributes.size() == 4u &&
          bindings.size() == 3u,
        "LocalVertexFactory must match fixed geometry streams including optional COLOR0 without selecting a Shader");
    check(render_data.release(manager).succeeded() && !render_data.is_drawable(),
        "releasing StaticMeshRenderData must close its complete drawable gate");

    toy3d::StaticMeshDesc uncolored_desc;
    uncolored_desc.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
    // Exercise the other fixed index-width alternative while leaving COLOR0
    // absent, which is a valid part of the first-stage stream contract.
    uncolored_desc.indices = std::vector<std::uint32_t>{0u, 1u, 2u};
    uncolored_desc.sections.push_back({0u, 3u, 0u});
    uncolored_desc.material_slots.push_back(material_instance);
    const toy3d::StaticMeshRef uncolored_mesh =
        toy3d::StaticMesh::create(std::move(uncolored_desc));
    check(uncolored_mesh != nullptr,
        "StaticMesh must allow the optional color stream to be absent");
    toy3d::StaticMeshRenderData uncolored_render_data(*uncolored_mesh);
    check(uncolored_render_data.begin_init(manager).succeeded() &&
          manager.record_pending_uploads(context).succeeded() &&
          uncolored_render_data.prepare_current_recording().succeeded() &&
          manager.commit_recording().succeeded(),
        "an uncolored mesh candidate must initialize as three required buffers");
    shader_inputs.resize(3u);
    check(uncolored_render_data.vertex_factory()->build_vertex_input(
              shader_inputs, layouts, attributes, bindings).succeeded(),
        "an uncolored LocalVertexFactory must match a Shader that does not require COLOR0");
    shader_inputs.resize(4u);
    shader_inputs[3].attribute_id = toy3d::ShaderVertexAttributeId::Color0;
    shader_inputs[3].component_count = 4u;
    shader_inputs[3].target_location = 3u;
    check(!uncolored_render_data.vertex_factory()->build_vertex_input(
              shader_inputs, layouts, attributes, bindings),
        "an uncolored LocalVertexFactory must reject a Shader that requires COLOR0");
    check(uncolored_render_data.index_buffer_binding().format ==
              toy3d::RHIIndexFormat::UInt32 &&
          uncolored_render_data.release(manager).succeeded(),
        "the uncolored candidate must preserve UInt32 indices and release normally");

    toy3d::TextureDesc invalid_texture_desc;
    invalid_texture_desc.width = 4;
    invalid_texture_desc.height = 4;
    invalid_texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    invalid_texture_desc.row_pitches = {17u};
    invalid_texture_desc.slice_pitches = {68u};
    invalid_texture_desc.mip_pixels = {
        std::vector<std::uint8_t>(68u, 0u)};
    check(toy3d::Texture::create(std::move(invalid_texture_desc)) == nullptr,
        "TextureDesc validation must reject a row pitch that splits PixelFormat blocks");

    toy3d::TextureDesc texture_desc;
    texture_desc.width = 4;
    texture_desc.height = 4;
    texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    texture_desc.row_pitches = {16u};
    texture_desc.slice_pitches = {64u};
    texture_desc.mip_pixels = {
        std::vector<std::uint8_t>(64u, 255u)};
    const toy3d::TextureRef texture =
        toy3d::Texture::create(std::move(texture_desc));
    check(texture != nullptr && texture->texture_resource() != nullptr,
        "a valid Texture must own a stable TextureResource allocation");
    toy3d::TextureResource* const texture_resource =
        texture->texture_resource();
    check(texture_resource->begin_init(manager).succeeded(),
        "TextureResource initial payload must enter the shared manager");
    const std::uint32_t texture_transitions_before = context.transition_count;
    check(manager.record_pending_uploads(context).succeeded() &&
          context.texture_upload_count == 1u &&
          context.transition_count - texture_transitions_before == 2u &&
          texture_resource->view_for_current_recording() != nullptr &&
          texture_resource->binding_generation() == 0u,
        "Texture initial upload must create a current-list view and record copy transitions without early publication");
    check(device.last_texture_desc.sample_count == 1u &&
          toy3d::rhi_has_all_flags(
              device.last_texture_desc.usage,
              toy3d::rhi_enum_or(
                  toy3d::RHIResourceUsage::ShaderResource,
                  toy3d::RHIResourceUsage::CopyDestination)),
        "TextureResource must apply the fixed sampled and copy-destination RHI policy");
    check(manager.discard_recording().succeeded() &&
          texture_resource->view_for_current_recording() == nullptr,
        "discarded initial Texture upload must retain CPU payload without publishing a view");
    check(manager.record_pending_uploads(context).succeeded() &&
          manager.commit_recording().succeeded() &&
          texture_resource->active_view() != nullptr &&
          texture_resource->binding_generation() == 1u,
        "submit commit must publish the first Texture view and nonzero binding generation");

    toy3d::ThreadManager material_thread_manager;
    toy3d::TaskGraphCreateResult material_graph_result =
        toy3d::create_task_graph(
            {0u, 256u, false}, material_thread_manager);
    check(material_graph_result.succeeded(),
        "Material fixture must create a single-thread Task Graph");
    std::unique_ptr<toy3d::TaskGraphInterface> material_graph =
        material_graph_result.take_task_graph();
    check(material_graph != nullptr &&
          material_graph->attach_to_thread(
              toy3d::NamedThread::GameThread).succeeded(),
        "Material fixture must attach its Game Thread");
    toy3d::RenderingThread material_rendering_thread(
        material_thread_manager, *material_graph,
        toy3d::RenderingThreadMode::SingleThread);
    check(material_rendering_thread.start().succeeded(),
        "Material fixture must open the RenderCommand facade");

    toy3d::RenderResourceManager frame_manager(device);
    std::unique_ptr<toy3d::RenderScene> frame_render_scene =
        std::make_unique<toy3d::RenderScene>(
            *material_graph, frame_manager);
    toy3d::RHITextureDesc present_texture_desc;
    present_texture_desc.width = 64u;
    present_texture_desc.height = 64u;
    present_texture_desc.format = toy3d::PixelFormat::B8G8R8A8UNorm;
    present_texture_desc.usage = toy3d::RHIResourceUsage::RenderTarget;
    present_texture_desc.initial_access = toy3d::RHIAccess::Present;
    const toy3d::RHITextureRef present_texture =
        std::make_shared<toy3d::RHITexture>(device, present_texture_desc);
    toy3d::RHITextureViewDesc present_view_desc;
    present_view_desc.type = toy3d::RHIResourceViewType::RenderTarget;
    present_view_desc.format = present_texture_desc.format;
    present_view_desc.subresources.mip_count = 1u;
    present_view_desc.subresources.layer_count = 1u;
    const toy3d::RHITextureViewRef present_view =
        std::make_shared<toy3d::RHITextureView>(
            present_texture, present_view_desc);

    toy3d::RHITextureDesc depth_texture_desc;
    depth_texture_desc.width = 64u;
    depth_texture_desc.height = 64u;
    depth_texture_desc.format = toy3d::PixelFormat::D32Float;
    depth_texture_desc.usage = toy3d::RHIResourceUsage::DepthStencil;
    depth_texture_desc.initial_access = toy3d::RHIAccess::DepthStencilWrite;
    const toy3d::RHITextureRef depth_texture =
        std::make_shared<toy3d::RHITexture>(device, depth_texture_desc);
    toy3d::RHITextureViewDesc depth_view_desc;
    depth_view_desc.type = toy3d::RHIResourceViewType::DepthStencil;
    depth_view_desc.format = depth_texture_desc.format;
    depth_view_desc.subresources.aspect = toy3d::RHITextureAspect::Depth;
    depth_view_desc.subresources.mip_count = 1u;
    depth_view_desc.subresources.layer_count = 1u;
    const toy3d::RHITextureViewRef depth_view =
        std::make_shared<toy3d::RHITextureView>(
            depth_texture, depth_view_desc);

    struct : toy3d::RHIFrameContext
    {
        toy3d::RHITextureRef color_texture;
        toy3d::RHITextureViewRef color_view;
        std::unique_ptr<toy3d::RHIGraphicsCommandContext> commands;

        const toy3d::RHITextureRef& present_texture() const override
        {
            return color_texture;
        }

        const toy3d::RHITextureViewRef& present_view() const override
        {
            return color_view;
        }

        std::uint32_t width() const override
        {
            return 64u;
        }

        std::uint32_t height() const override
        {
            return 64u;
        }

        toy3d::RHIResult<
            std::unique_ptr<toy3d::RHIGraphicsCommandContext>>
        create_graphics_command_context() override
        {
            return toy3d::RHIResult<
                std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::success(
                    std::move(commands));
        }
    } frame_context_shape;

    struct : toy3d::RHIViewportContext
    {
        std::unique_ptr<toy3d::RHIFrameContext> next_frame;
        std::vector<std::string>* operations = nullptr;
        std::uint32_t begin_count = 0u;
        std::uint32_t end_count = 0u;
        std::uint32_t abort_count = 0u;
        bool end_success = true;
        toy3d::RHIQueueCompletionValue next_completion_value = 42u;
        toy3d::RHIStatus next_presentation_status =
            toy3d::RHIStatus::success();

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>>
        begin_frame() override
        {
            ++begin_count;
            if (operations != nullptr)
            {
                operations->push_back("begin_frame");
            }
            return toy3d::RHIResult<
                std::unique_ptr<toy3d::RHIFrameContext>>::success(
                    std::move(next_frame));
        }

        toy3d::RHIResult<toy3d::RHIFrameEndResult> end_frame(
            std::unique_ptr<toy3d::RHIFrameContext> frame,
            const std::vector<toy3d::RHICommandListRef>& command_lists)
            override
        {
            ++end_count;
            if (operations != nullptr)
            {
                operations->push_back("end_frame");
            }
            if (!frame || command_lists.size() != 1u ||
                !command_lists.front())
            {
                return toy3d::RHIResult<toy3d::RHIFrameEndResult>::failure(
                    toy3d::RHIErrorCode::InvalidArgument,
                    "Frame-owner smoke requires one immutable business list");
            }
            if (!end_success)
            {
                return toy3d::RHIResult<toy3d::RHIFrameEndResult>::failure(
                    toy3d::RHIErrorCode::BackendFailure,
                    "Injected frame-owner submit failure");
            }
            toy3d::RHIFrameEndResult result;
            result.completion_value = next_completion_value;
            result.presentation_status = next_presentation_status;
            return toy3d::RHIResult<toy3d::RHIFrameEndResult>::success(
                std::move(result));
        }

        toy3d::RHIStatus abort_frame(
            std::unique_ptr<toy3d::RHIFrameContext> frame) override
        {
            ++abort_count;
            if (operations != nullptr)
            {
                operations->push_back("abort_frame");
            }
            return frame
                ? toy3d::RHIStatus::success()
                : toy3d::RHIStatus::failure(
                    toy3d::RHIErrorCode::InvalidArgument,
                    "Frame-owner smoke cannot abort an empty frame");
        }

        toy3d::RHIStatus request_resize(
            std::uint32_t, std::uint32_t) override
        {
            return toy3d::RHIStatus::success();
        }
    } frame_viewport;

    const auto make_frame = [&]()
        -> std::unique_ptr<toy3d::RHIFrameContext>
    {
        auto frame_context =
            std::make_unique<decltype(frame_context_shape)>();
        frame_context->color_texture = present_texture;
        frame_context->color_view = present_view;
        auto frame_commands = std::make_unique<decltype(context)>();
        frame_commands->operations = frame_viewport.operations;
        frame_commands->command_device = &device;
        frame_commands->finish_success = true;
        frame_context->commands = std::move(frame_commands);
        return frame_context;
    };
    const auto make_valid_view_family = [&]()
    {
        std::vector<toy3d::SceneView> views;
        views.emplace_back(
            toy3d::Vector3(),
            toy3d::Quaternion::identity(),
            toy3d::Vector3(0.0f, 0.0f, 1.0f),
            toy3d::UIntVector2(),
            toy3d::UIntVector2(64u, 64u),
            toy3d::UIntVector2(64u, 64u),
            toy3d::CameraProjectionMode::Perspective,
            toy3d::Radians(1.0f),
            0.1f,
            100.0f);
        return toy3d::SceneViewFamily(
            *frame_render_scene,
            toy3d::UIntVector2(64u, 64u),
            std::move(views));
    };

    decltype(ready_first) submitted_frame_resource;
    std::vector<std::string> submitted_frame_operations;
    submitted_frame_resource.operations = &submitted_frame_operations;
    frame_viewport.operations = &submitted_frame_operations;
    frame_viewport.next_frame = make_frame();
    check(frame_manager.begin_init(submitted_frame_resource).succeeded(),
        "frame-owner smoke must begin a pending resource transaction");
    std::vector<toy3d::SceneView> submitted_views;
    submitted_views.emplace_back(
        toy3d::Vector3(),
        toy3d::Quaternion::identity(),
        toy3d::Vector3(0.0f, 0.0f, 1.0f),
        toy3d::UIntVector2(),
        toy3d::UIntVector2(64u, 64u),
        toy3d::UIntVector2(64u, 64u),
        toy3d::CameraProjectionMode::Perspective,
        toy3d::Radians(1.0f),
        0.1f,
        100.0f);
    toy3d::ForwardSceneRenderer submitted_frame_renderer(
        toy3d::SceneViewFamily(
            *frame_render_scene,
            toy3d::UIntVector2(64u, 64u),
            std::move(submitted_views)));
    const toy3d::RHIResult<toy3d::RHIFrameEndResult>
        submitted_frame_result = submitted_frame_renderer.render_frame(
            *frame_render_scene,
            device,
            frame_manager,
            frame_viewport,
            depth_view);
    const std::vector<std::string> expected_submitted_operations = {
        "begin_frame",
        "begin_recording",
        "record_pending_uploads",
        "transition",
        "begin_render_pass",
        "end_render_pass",
        "transition",
        "finish_recording",
        "end_frame",
        "commit_recording"};
    check(submitted_frame_result.succeeded() &&
          submitted_frame_result.value().completion_value == 42u &&
          submitted_frame_resource.state() ==
              toy3d::RenderResourceState::Ready &&
          submitted_frame_operations == expected_submitted_operations &&
          frame_viewport.end_count == 1u &&
          frame_viewport.abort_count == 0u,
        "frame owner must record uploads and Base Pass in one list, then commit only after submit");
    check(frame_manager.release(submitted_frame_resource).succeeded(),
        "submitted frame resource must release after the frame-owner smoke");

    decltype(ready_first) submit_failed_resource;
    std::vector<std::string> submit_failed_operations;
    submit_failed_resource.operations = &submit_failed_operations;
    frame_viewport.operations = &submit_failed_operations;
    frame_viewport.next_frame = make_frame();
    frame_viewport.end_success = false;
    check(frame_manager.begin_init(submit_failed_resource).succeeded(),
        "submit failure smoke must begin a retryable pending transaction");
    toy3d::ForwardSceneRenderer submit_failed_renderer(
        make_valid_view_family());
    const toy3d::RHIResult<toy3d::RHIFrameEndResult>
        submit_failed_result = submit_failed_renderer.render_frame(
            *frame_render_scene,
            device,
            frame_manager,
            frame_viewport,
            depth_view);
    const std::vector<std::string> expected_submit_failed_operations = {
        "begin_frame",
        "begin_recording",
        "record_pending_uploads",
        "transition",
        "begin_render_pass",
        "end_render_pass",
        "transition",
        "finish_recording",
        "end_frame",
        "discard_recording"};
    check(!submit_failed_result &&
          submit_failed_resource.state() ==
              toy3d::RenderResourceState::PendingUpload &&
          submit_failed_resource.discard_count == 1 &&
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
        toy3d::RHIStatus::failure(
            toy3d::RHIErrorCode::Suboptimal,
            "Injected recoverable suboptimal presentation");
    check(frame_manager.begin_init(suboptimal_present_resource).succeeded(),
        "Suboptimal presentation smoke must begin a pending transaction");
    toy3d::ForwardSceneRenderer suboptimal_present_renderer(
        make_valid_view_family());
    const toy3d::RHIResult<toy3d::RHIFrameEndResult>
        suboptimal_present_result = suboptimal_present_renderer.render_frame(
            *frame_render_scene,
            device,
            frame_manager,
            frame_viewport,
            depth_view);
    check(suboptimal_present_result.succeeded() &&
          suboptimal_present_result.value().completion_value == 43u &&
          suboptimal_present_result.value().presentation_status.code() ==
              toy3d::RHIErrorCode::Suboptimal &&
          suboptimal_present_resource.state() ==
              toy3d::RenderResourceState::Ready &&
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
        toy3d::RHIStatus::failure(
            toy3d::RHIErrorCode::OutOfDate,
            "Injected recoverable out-of-date presentation");
    check(frame_manager.begin_init(out_of_date_present_resource).succeeded(),
        "OutOfDate presentation smoke must begin a pending transaction");
    toy3d::ForwardSceneRenderer out_of_date_present_renderer(
        make_valid_view_family());
    const toy3d::RHIResult<toy3d::RHIFrameEndResult>
        out_of_date_present_result =
            out_of_date_present_renderer.render_frame(
                *frame_render_scene,
                device,
                frame_manager,
                frame_viewport,
                depth_view);
    check(out_of_date_present_result.succeeded() &&
          out_of_date_present_result.value().completion_value == 44u &&
          out_of_date_present_result.value().presentation_status.code() ==
              toy3d::RHIErrorCode::OutOfDate &&
          out_of_date_present_resource.state() ==
              toy3d::RenderResourceState::Ready &&
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
    toy3d::ForwardSceneRenderer unknown_boundary_renderer(
        make_valid_view_family());
    const toy3d::RHIResult<toy3d::RHIFrameEndResult>
        unknown_boundary_result = unknown_boundary_renderer.render_frame(
            *frame_render_scene,
            device,
            frame_manager,
            frame_viewport,
            depth_view);
    check(unknown_boundary_result.succeeded() &&
          unknown_boundary_result.value().completion_value == 0u &&
          unknown_boundary_result.value().presentation_status.code() ==
              toy3d::RHIErrorCode::BackendFailure &&
          unknown_boundary_resource.state() ==
              toy3d::RenderResourceState::Ready &&
          unknown_boundary_operations == expected_submitted_operations,
        "unknown completion metadata after submit must surface terminal presentation status without rolling back resources");
    check(frame_manager.release(unknown_boundary_resource).succeeded(),
        "unknown-boundary resource must release after commit");
    frame_viewport.next_completion_value = 42u;
    frame_viewport.next_presentation_status = toy3d::RHIStatus::success();

    decltype(ready_first) aborted_frame_resource;
    std::vector<std::string> aborted_frame_operations;
    aborted_frame_resource.operations = &aborted_frame_operations;
    frame_viewport.operations = &aborted_frame_operations;
    frame_viewport.next_frame = make_frame();
    check(frame_manager.begin_init(aborted_frame_resource).succeeded(),
        "abort smoke must begin a retryable pending resource transaction");
    std::vector<toy3d::SceneView> invalid_views;
    invalid_views.emplace_back(
        toy3d::Vector3(),
        toy3d::Quaternion::identity(),
        toy3d::Vector3(0.0f, 0.0f, 1.0f),
        toy3d::UIntVector2(),
        toy3d::UIntVector2(),
        toy3d::UIntVector2(64u, 64u),
        toy3d::CameraProjectionMode::Perspective,
        toy3d::Radians(1.0f),
        0.1f,
        100.0f);
    toy3d::ForwardSceneRenderer aborted_frame_renderer(
        toy3d::SceneViewFamily(
            *frame_render_scene,
            toy3d::UIntVector2(64u, 64u),
            std::move(invalid_views)));
    const toy3d::RHIResult<toy3d::RHIFrameEndResult>
        aborted_frame_result = aborted_frame_renderer.render_frame(
            *frame_render_scene,
            device,
            frame_manager,
            frame_viewport,
            depth_view);
    const std::vector<std::string> expected_aborted_operations = {
        "begin_frame",
        "begin_recording",
        "record_pending_uploads",
        "discard_recording",
        "abort_frame"};
    check(!aborted_frame_result &&
          aborted_frame_resource.state() ==
              toy3d::RenderResourceState::PendingUpload &&
          aborted_frame_resource.discard_count == 1 &&
          aborted_frame_operations == expected_aborted_operations &&
          frame_viewport.abort_count == 1u,
        "invalid init_views after acquire must discard resource publication and abort exactly once");
    check(frame_manager.release(aborted_frame_resource).succeeded(),
        "aborted frame resource must remain releasable after discard");
    frame_render_scene.reset();

    toy3d::ShaderMapProgramData active_program_data =
        make_material_program("Main", 20u);
    active_program_data.graphics_pass_state.cull_mode =
        toy3d::shader::ShaderGraphicsPassState::CullMode::Front;
    active_program_data.pass_template_hash =
        toy3d::shader::calculate_shader_graphics_pass_state_hash(
            active_program_data.graphics_pass_state);
    const toy3d::ShaderMapProgramRef active_program =
        load_program(std::move(active_program_data));
    const toy3d::ShaderMapProgramRef candidate_program =
        load_program(make_material_program("Candidate", 40u));
    toy3d::ShaderMapProgramData incompatible_schema_program_data =
        make_material_program("IncompleteCandidate", 60u);
    incompatible_schema_program_data.bindings.front().constant_members.erase(
        incompatible_schema_program_data.bindings.front().constant_members.begin());
    incompatible_schema_program_data.stages.front().reflection =
        incompatible_schema_program_data.bindings;
    const toy3d::ShaderMapProgramRef incompatible_schema_program =
        load_program(std::move(incompatible_schema_program_data));
    toy3d::MaterialDesc render_material_desc;
    render_material_desc.shader_name = "Toy3d/Test/Material";
    render_material_desc.shader_program = active_program;
    render_material_desc.scalar_defaults.emplace(11u, 0.25f);
    render_material_desc.vector4_defaults.emplace(
        13u, toy3d::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    render_material_desc.texture_defaults.emplace(12u, texture);
    const toy3d::MaterialRef render_material =
        toy3d::Material::create(std::move(render_material_desc));
    check(render_material != nullptr,
        "Material fixture requires a schema-compatible Material");

    const std::uint32_t invisible_buffer_count = device.buffer_creation_count;
    const std::uint32_t invisible_binding_count =
        device.binding_set_creation_count;
    toy3d::MaterialInstanceRef invisible_material_instance =
        toy3d::MaterialInstance::create(render_material);
    invisible_material_instance.reset();
    check(device.buffer_creation_count == invisible_buffer_count &&
          device.binding_set_creation_count == invisible_binding_count,
        "an invisible MaterialInstance must not allocate constants or bindings");

    toy3d::MaterialInstanceRef render_material_instance =
        toy3d::MaterialInstance::create(render_material);
    check(render_material_instance != nullptr &&
          !render_material_instance->set_vector(
              11u, toy3d::vec4(1.0f, 0.0f, 0.0f, 1.0f)),
        "a setter with the wrong reflected type must fail without mutation");
    check(render_material_instance->set_scalar(11u, 0.5f) &&
          render_material_instance->set_scalar(11u, 0.75f) &&
          render_material_instance->set_texture(12u, texture),
        "valid Material setters must enqueue their FIFO proxy updates");

    toy3d::RHIBindingLayoutDesc material_layout_desc;
    material_layout_desc.entries = {
        {toy3d::RHIBindingGroup::Material, 0u,
            toy3d::RHIResourceBindingType::UniformBuffer,
            toy3d::RHIShaderStageFlags::Vertex, 1u},
        {toy3d::RHIBindingGroup::Material, 1u,
            toy3d::RHIResourceBindingType::SampledTexture,
            toy3d::RHIShaderStageFlags::Vertex, 1u}};
    material_layout_desc.debug_name = "MaterialTestLayout";
    const toy3d::RHIBindingLayoutRef material_layout =
        std::make_shared<toy3d::RHIBindingLayout>(
            device, std::move(material_layout_desc));
    toy3d::MaterialRenderProxy* const material_proxy =
        render_material_instance->material_render_proxy();
    const std::uint32_t first_material_binding_count =
        device.binding_set_creation_count;
    toy3d::RHIResult<toy3d::RHIBindingSetRef> first_material_binding =
        material_proxy->materialize(device, context, material_layout);
    float materialized_scalar = 0.0f;
    if (context.last_buffer_upload_data.size() >= sizeof(float))
    {
        std::memcpy(&materialized_scalar,
            context.last_buffer_upload_data.data(), sizeof(float));
    }
    check(first_material_binding.succeeded() &&
          device.binding_set_creation_count == first_material_binding_count + 1u &&
          materialized_scalar == 0.75f,
        "first visible materialization must consume the final FIFO scalar value");
    check(material_proxy->materialize(
              device, context, material_layout).succeeded() &&
          device.binding_set_creation_count == first_material_binding_count + 1u,
        "unchanged Material parameters and Texture binding identity must reuse the binding");

    const toy3d::RHITextureViewRef first_view =
        texture_resource->active_view();
    toy3d::TextureDesc content_update = texture->desc();
    content_update.mip_pixels[0].assign(64u, 127u);
    check(texture_resource->update(std::move(content_update), manager) &&
          manager.record_pending_uploads(context) &&
          texture_resource->view_for_current_recording() == first_view &&
          manager.commit_recording() &&
          texture_resource->active_view() == first_view &&
          texture_resource->binding_generation() == 1u,
        "same-layout Texture content update must keep view identity and binding generation");
    check(material_proxy->materialize(
              device, context, material_layout).succeeded() &&
          device.binding_set_creation_count == first_material_binding_count + 1u,
        "same-view Texture content updates must not rebuild Material bindings");

    toy3d::TextureDesc replacement_desc;
    replacement_desc.width = 8;
    replacement_desc.height = 4;
    replacement_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    replacement_desc.row_pitches = {32u};
    replacement_desc.slice_pitches = {128u};
    replacement_desc.mip_pixels = {
        std::vector<std::uint8_t>(128u, 63u)};
    check(texture_resource->update(std::move(replacement_desc), manager) &&
          manager.record_pending_uploads(context) &&
          texture_resource->view_for_current_recording() != first_view &&
          texture_resource->active_view() == first_view &&
          texture_resource->binding_generation() == 1u,
        "Texture replacement candidate must be current-list eligible without replacing active before submit");
    check(material_proxy->materialize(
              device, context, material_layout).succeeded() &&
          device.binding_set_creation_count == first_material_binding_count + 2u,
        "a replacement candidate view must invalidate the current-list Material binding");
    check(manager.commit_recording() &&
          texture_resource->active_view() != first_view &&
          texture_resource->binding_generation() == 2u &&
          first_view != nullptr,
        "Texture replacement commit must increment generation while prior strong view references remain valid");
    check(material_proxy->materialize(
              device, context, material_layout).succeeded() &&
          device.binding_set_creation_count == first_material_binding_count + 3u,
        "a committed Texture binding generation change must invalidate cached Material bindings");

    const toy3d::RHIResult<toy3d::RHIBindingSetRef> active_candidate_binding =
        material_proxy->materialize(device, context, material_layout);
    check(active_candidate_binding.succeeded() &&
          material_proxy->effective_graphics_pass_state() != nullptr &&
          material_proxy->effective_graphics_pass_state()->cull_mode ==
              toy3d::shader::ShaderGraphicsPassState::CullMode::Front,
        "a single-sided active candidate must preserve the Shader Pass cull mode");

    const toy3d::RHIStatus direct_incomplete_stage =
        material_proxy->stage_material_candidate(candidate_program, true);
    const toy3d::RHIStatus direct_incomplete_commit =
        material_proxy->commit_material_candidate();
    const toy3d::RHIStatus direct_retry_stage =
        material_proxy->stage_material_candidate(candidate_program, true);
    check(direct_incomplete_stage && !direct_incomplete_commit &&
          direct_retry_stage,
        "a failed direct candidate commit must discard staged state and allow retry");
    material_proxy->discard_material_candidate();

    const bool incomplete_candidate_staged =
        render_material_instance->stage_material_replacement(
            incompatible_schema_program, true);
    const bool incomplete_candidate_published =
        render_material_instance->publish_material_replacement();
    const toy3d::RHIResult<toy3d::RHIBindingSetRef>
        active_binding_after_failed_candidate =
            material_proxy->materialize(device, context, material_layout);
    const bool old_schema_scalar_accepted =
        render_material_instance->set_scalar(11u, 0.75f);
    check(incomplete_candidate_staged && incomplete_candidate_published &&
          material_proxy->shader_program() == active_program &&
          material_proxy->effective_graphics_pass_state() != nullptr &&
          material_proxy->effective_graphics_pass_state()->cull_mode ==
              toy3d::shader::ShaderGraphicsPassState::CullMode::Front &&
          active_binding_after_failed_candidate.succeeded() &&
          active_binding_after_failed_candidate.value() ==
              active_candidate_binding.value() && old_schema_scalar_accepted,
        "a failed public two-sided publication must preserve the complete active state and GT schema");

    check(render_material_instance->stage_material_replacement(
              candidate_program, true) &&
          material_proxy->materialize_staged(
              device, context, material_layout).succeeded() &&
          render_material_instance->discard_material_replacement() &&
          material_proxy->shader_program() == active_program &&
          material_proxy->effective_graphics_pass_state() != nullptr &&
          material_proxy->effective_graphics_pass_state()->cull_mode ==
              toy3d::shader::ShaderGraphicsPassState::CullMode::Front,
        "discarding a fully materialized candidate must preserve active Program and state");

    const bool staged_two_sided = render_material_instance->stage_material_replacement(
        candidate_program, true);
    toy3d::RHIResult<toy3d::RHIBindingSetRef> staged_two_sided_binding =
        material_proxy->materialize_staged(
            device, context, material_layout);
    check(staged_two_sided && staged_two_sided_binding.succeeded() &&
          render_material_instance->publish_material_replacement() &&
          material_proxy->shader_program() == candidate_program &&
          material_proxy->effective_graphics_pass_state() != nullptr &&
          material_proxy->effective_graphics_pass_state()->cull_mode ==
              toy3d::shader::ShaderGraphicsPassState::CullMode::None &&
          material_proxy->materialize(
              device, context, material_layout).value() ==
              staged_two_sided_binding.value(),
        "two-sided publication must atomically commit Program, CullMode::None, and binding");

    check(render_material_instance->stage_material_replacement(
              candidate_program, false) &&
          material_proxy->materialize_staged(
              device, context, material_layout).succeeded() &&
          render_material_instance->publish_material_replacement() &&
          material_proxy->effective_graphics_pass_state() != nullptr &&
          material_proxy->effective_graphics_pass_state()->cull_mode ==
              candidate_program->data().graphics_pass_state.cull_mode,
        "disabling two-sided must restore the Shader Pass cull mode in the next candidate");
    toy3d::MaterialInstance::release(render_material_instance);
    check(!render_material_instance &&
          material_rendering_thread.stop().succeeded(),
        "Material proxy release must execute before Rendering Thread teardown");
    check(material_graph->shutdown(
              toy3d::TaskGraphShutdownMode::CancelPending).succeeded(),
        "Material fixture Task Graph must shut down cleanly");
    material_graph.reset();

    check(texture_resource->release(manager).succeeded(),
        "TextureResource release must detach manager state without waiting for GPU completion");

    deterministic.deterministic_failure = true;
    check(manager.begin_init(deterministic).succeeded(),
        "deterministic failure resource must begin as PendingUpload");
    const toy3d::RHIStatus deterministic_status =
        manager.record_pending_uploads(context);
    check(!deterministic_status &&
          deterministic.state() == toy3d::RenderResourceState::Failed &&
          deterministic.failure_status().code() == toy3d::RHIErrorCode::Unsupported,
        "resource-local Unsupported must publish Failed with the original diagnostic");
    check(manager.discard_recording() && manager.release(deterministic),
        "Failed resources must be removable and releasable");

    check(manager.begin_init(released_while_recording) &&
          manager.record_pending_uploads(context),
        "pending release smoke must first enter the current recording collection");
    check(manager.release(released_while_recording) &&
          released_while_recording.state() ==
              toy3d::RenderResourceState::Released &&
          released_while_recording.discard_count == 1 &&
          released_while_recording.release_count == 1,
        "release before submit must discard Ready publication and release only CPU refs");
    check(manager.commit_recording().succeeded(),
        "commit after a removed recording entry must not republish the released resource");

    toy3d::RenderResourceManager terminal_manager(device);
    check(terminal_manager.begin_init(terminal_first) &&
          terminal_manager.begin_init(terminal_second) &&
          terminal_manager.record_pending_uploads(context),
        "terminal smoke must establish pending and recording non-owning entries");
    check(terminal_manager.clear_for_terminal() &&
          terminal_first.discard_count == 1 &&
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
