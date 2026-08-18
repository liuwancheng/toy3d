#include "scene_render.h"

namespace toy3d
{
    /*****************   SceneRendering  *****************/
    SceneRendering::SceneRendering(
        RHIDevice& device,
        ShaderBytecodeProvider& shader_bytecode_provider_value)
        : rhi_device(device),
          shader_bytecode_provider(shader_bytecode_provider_value)
    {

    }

    void SceneRendering::init_views()
    {

    }

    void SceneRendering::frustum_cull()
    {

    }

    void SceneRendering::allocate_scene_rt()
    {
    }

    RHIResult<RHICommandListRef> SceneRendering::render(RHIFrameContext& frame)
    {
        RHIStatus status = initialize_test_pass_resources();
        if (!status)
        {
            return RHIResult<RHICommandListRef>::failure(status.code(), status.message());
        }
        status = initialize_test_depth_resources(frame);
        if (!status)
        {
            return RHIResult<RHICommandListRef>::failure(status.code(), status.message());
        }
        auto context_result = frame.create_graphics_command_context();
        if (!context_result)
        {
            return RHIResult<RHICommandListRef>::failure(
                context_result.status().code(),
                context_result.status().message());
        }
        std::unique_ptr<RHIGraphicsCommandContext> context = std::move(context_result).value();
        status = context->begin_recording("SceneRendering");
        if (status)
        {
            status = prepare_test_pass_texture(*context);
        }
        if (status)
        {
            status = prepare_test_pass_depth(*context);
        }
        if (status)
        {
            RHIResourceTransition transition;
            transition.resource = frame.present_texture();
            transition.before = RHIAccess::Present;
            transition.after = RHIAccess::RenderTarget;
            status = context->transition_resources({transition});
        }
        if (status)
        {
            status = render_test_pass(*context, frame);
        }
        if (status)
        {
            RHIResourceTransition transition;
            transition.resource = frame.present_texture();
            transition.before = RHIAccess::RenderTarget;
            transition.after = RHIAccess::Present;
            status = context->transition_resources({transition});
        }
        if (!status)
        {
            return RHIResult<RHICommandListRef>::failure(status.code(), status.message());
        }
        return context->finish_recording();
    }

}
