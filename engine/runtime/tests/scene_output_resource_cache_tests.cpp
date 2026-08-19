#include "renderscene/output/scene_output_resource_cache.h"

#include "drivers/rhi/rhi_queue.h"

#include <iostream>
#include <memory>
#include <string>
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

    template<typename T>
    toy3d::RHIResult<T> unsupported()
    {
        return toy3d::RHIResult<T>::failure(
            toy3d::RHIErrorCode::Unsupported,
            "Not required by this test.");
    }

    class FakeQueue final : public toy3d::RHIQueue
    {
    public:
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
            return unsupported<toy3d::RHISubmitResult>();
        }
    };

    class FakeDevice final : public toy3d::RHIDevice
    {
    public:
        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override
        {
            initialized_ = true;
            return toy3d::RHIStatus::success();
        }

        const toy3d::RHICapabilities& capabilities() const override
        {
            return capabilities_;
        }

        const toy3d::RHILimits& limits() const override
        {
            return limits_;
        }

        toy3d::RHIFormatCapabilities format_capabilities(
            toy3d::RHIFormat format) const override
        {
            toy3d::RHIFormatCapabilities result;
            if (format == toy3d::RHIFormat::R8G8B8A8UNorm)
            {
                result.usage = toy3d::rhi_enum_or(
                    toy3d::RHIFormatUsage::Sampled,
                    toy3d::RHIFormatUsage::RenderTarget);
            }
            return result;
        }

        toy3d::RHIQueue& graphics_queue() override
        {
            return queue_;
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>
            create_viewport_context(
                const toy3d::RHISurfaceRef&,
                const toy3d::RHIViewportContextDesc&) override
        {
            return unsupported<std::unique_ptr<toy3d::RHIViewportContext>>();
        }

        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer(
            const toy3d::RHIBufferDesc&,
            const toy3d::RHIInitialData* = nullptr) override
        {
            return unsupported<toy3d::RHIBufferRef>();
        }

        toy3d::RHIResult<toy3d::RHITextureRef> create_texture(
            const toy3d::RHITextureDesc& desc,
            const toy3d::RHIInitialData* initial_data = nullptr) override
        {
            if (initial_data != nullptr)
            {
                return toy3d::RHIResult<toy3d::RHITextureRef>::failure(
                    toy3d::RHIErrorCode::InvalidArgument,
                    "SceneOutput textures must not use creation-time data.");
            }
            ++texture_create_count;
            if (fail_texture_create_number == texture_create_count)
            {
                return toy3d::RHIResult<toy3d::RHITextureRef>::failure(
                    injected_failure,
                    "Injected SceneOutput texture failure.");
            }
            texture_descs.push_back(desc);
            auto texture = std::make_shared<toy3d::RHITexture>(desc);
            created_textures.push_back(texture);
            return toy3d::RHIResult<toy3d::RHITextureRef>::success(
                std::move(texture));
        }

        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view(
            const toy3d::RHIBufferRef&,
            const toy3d::RHIBufferViewDesc&) override
        {
            return unsupported<toy3d::RHIBufferViewRef>();
        }

        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view(
            const toy3d::RHITextureRef& texture,
            const toy3d::RHITextureViewDesc& desc) override
        {
            ++view_create_count;
            if (fail_view_create_number == view_create_count)
            {
                return toy3d::RHIResult<toy3d::RHITextureViewRef>::failure(
                    injected_failure,
                    "Injected SceneOutput view failure.");
            }
            view_descs.push_back(desc);
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                std::make_shared<toy3d::RHITextureView>(texture, desc));
        }

        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler(
            const toy3d::RHISamplerDesc&) override
        {
            return unsupported<toy3d::RHISamplerRef>();
        }

        toy3d::RHIResult<toy3d::RHIBindingSetRef> create_binding_set(
            const toy3d::RHIBindingSetDesc&) override
        {
            return unsupported<toy3d::RHIBindingSetRef>();
        }

        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence(
            const std::string&) override
        {
            return unsupported<toy3d::RHIGPUFenceRef>();
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>
            create_graphics_command_context() override
        {
            return unsupported<
                std::unique_ptr<toy3d::RHIGraphicsCommandContext>>();
        }

        std::size_t texture_create_count = 0;
        std::size_t view_create_count = 0;
        std::size_t fail_texture_create_number = 0;
        std::size_t fail_view_create_number = 0;
        toy3d::RHIErrorCode injected_failure =
            toy3d::RHIErrorCode::OutOfMemory;
        std::vector<toy3d::RHITextureDesc> texture_descs;
        std::vector<toy3d::RHITextureViewDesc> view_descs;
        std::vector<std::weak_ptr<toy3d::RHITexture>> created_textures;

    protected:
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(
            const toy3d::RHIShaderDesc&) override
        {
            return unsupported<toy3d::RHIShaderRef>();
        }

        toy3d::RHIResult<toy3d::RHIBindingLayoutRef>
            create_binding_layout_impl(
                const toy3d::RHIBindingLayoutDesc&) override
        {
            return unsupported<toy3d::RHIBindingLayoutRef>();
        }

        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>
            create_graphics_pipeline_impl(
                const toy3d::RHIGraphicsPipelineDesc&) override
        {
            return unsupported<toy3d::RHIGraphicsPipelineRef>();
        }

        bool is_initialized_impl() const override
        {
            return initialized_;
        }

        toy3d::RHIStatus wait_idle_before_shutdown_impl() override
        {
            return toy3d::RHIStatus::success();
        }

        toy3d::RHIStatus shutdown_impl() override
        {
            initialized_ = false;
            return toy3d::RHIStatus::success();
        }

    private:
        bool initialized_ = true;
        toy3d::RHICapabilities capabilities_;
        toy3d::RHILimits limits_;
        FakeQueue queue_;
    };

    toy3d::SceneOutputUpdate make_update(
        std::uint64_t id,
        std::uint64_t revision,
        toy3d::SceneOutputExtent extent)
    {
        toy3d::SceneOutputUpdate update;
        update.output_id = toy3d::SceneOutputId(id);
        update.revision = toy3d::SceneOutputRevision(revision);
        update.extent = extent;
        return update;
    }
}

int main()
{
    using namespace toy3d;

    FakeDevice device;
    SceneOutputResourceCache cache(device);
    const SceneOutputId first_id(1);
    SceneOutputApplyResult apply = cache.apply_updates({
        make_update(1, 1, {640, 480})});
    SceneOutputResourceRef first = cache.find(first_id);
    check(apply.succeeded() && apply.updated_count == 1 &&
            first != nullptr && first->revision == SceneOutputRevision(1) &&
            first->texture != nullptr &&
            first->render_target_view != nullptr &&
            first->shader_resource_view != nullptr,
        "A non-zero SceneOutput update must atomically publish texture, RTV, and SRV");
    check(device.texture_descs.size() == 1 &&
            device.texture_descs[0].format == RHIFormat::R8G8B8A8UNorm &&
            rhi_has_all_flags(
                device.texture_descs[0].usage,
                rhi_enum_or(
                    RHIResourceUsage::RenderTarget,
                    RHIResourceUsage::ShaderResource)) &&
            device.texture_descs[0].initial_access == RHIAccess::Common &&
            device.view_descs.size() == 2 &&
            device.view_descs[0].type == RHIResourceViewType::RenderTarget &&
            device.view_descs[1].type == RHIResourceViewType::ShaderResource,
        "SceneOutput resources must use the linear SDR render-to-sample RHI contract");

    const SceneOutputResourceRef retained_first = first;
    apply = cache.apply_updates({make_update(1, 2, {800, 600})});
    first = cache.find(first_id);
    check(apply.succeeded() && first != retained_first &&
            first->extent == SceneOutputExtent{800, 600} &&
            retained_first->extent == SceneOutputExtent{640, 480} &&
            retained_first->texture != nullptr,
        "Resize must publish a new immutable version without invalidating retained old versions");

    apply = cache.apply_updates({make_update(1, 2, {1024, 768})});
    check(!apply.succeeded() &&
            apply.status.code() == RHIErrorCode::InvalidArgument &&
            cache.find(first_id) == first,
        "An equal revision with different payload must fail without replacing the cache");
    apply = cache.apply_updates({make_update(1, 1, {1024, 768})});
    check(!apply.succeeded() && cache.latest_revision(first_id) ==
            SceneOutputRevision(2),
        "A lower revision must fail without changing the current metadata");
    apply = cache.apply_updates({make_update(1, 3, {800, 600})});
    check(!apply.succeeded() && cache.latest_revision(first_id) ==
            SceneOutputRevision(2),
        "A newer revision that does not resize must be rejected as a protocol error");

    const std::size_t texture_count_before_protocol_failures =
        device.texture_create_count;
    apply = cache.apply_updates({
        make_update(4, 1, {64, 64}),
        make_update(4, 2, {128, 128})});
    check(!apply.succeeded() &&
            device.texture_create_count ==
                texture_count_before_protocol_failures,
        "Duplicate IDs must fail validation before allocating candidate resources");
    apply = cache.apply_updates({make_update(4, 1, {64, 0})});
    check(!apply.succeeded(),
        "A partially zero SceneOutput extent must be rejected");
    SceneOutputUpdate unknown_release = make_update(9, 1, {});
    unknown_release.operation = SceneOutputUpdateOperation::Release;
    apply = cache.apply_updates({unknown_release});
    check(!apply.succeeded(),
        "A release without a live SceneOutput must be rejected");
    SceneOutputUpdate invalid_operation = make_update(4, 1, {64, 64});
    invalid_operation.operation =
        static_cast<SceneOutputUpdateOperation>(255);
    apply = cache.apply_updates({invalid_operation});
    check(!apply.succeeded(),
        "An out-of-domain SceneOutput update operation must be rejected");

    const SceneOutputUpdate release_first = []()
    {
        SceneOutputUpdate update = make_update(1, 3, {});
        update.operation = SceneOutputUpdateOperation::Release;
        return update;
    }();
    SceneOutputUpdate release_with_extent = release_first;
    release_with_extent.extent = {800, 600};
    apply = cache.apply_updates({release_with_extent});
    check(!apply.succeeded() && cache.find(first_id) == first,
        "A release payload must not carry an extent");
    const std::size_t next_view_failure = device.view_create_count + 2;
    device.fail_view_create_number = next_view_failure;
    const std::size_t texture_count_before_failed_batch =
        device.created_textures.size();
    apply = cache.apply_updates({
        release_first,
        make_update(2, 1, {320, 200}),
        make_update(3, 1, {128, 128})});
    check(!apply.succeeded() &&
            apply.status.code() == RHIErrorCode::OutOfMemory &&
            cache.find(first_id) == first &&
            !cache.is_released(first_id) &&
            cache.find(SceneOutputId(2)) == nullptr &&
            cache.find(SceneOutputId(3)) == nullptr,
        "A candidate view failure must publish none of a mixed release/create batch");
    check(device.created_textures.size() ==
            texture_count_before_failed_batch + 1 &&
            device.created_textures.back().expired(),
        "An unpublished candidate texture must be released after batch failure");
    device.fail_view_create_number = 0;

    apply = cache.apply_updates({
        release_first,
        make_update(2, 1, {})});
    check(apply.succeeded() && apply.released_count == 1 &&
            apply.updated_count == 1 && cache.find(first_id) == nullptr &&
            cache.is_released(first_id) &&
            cache.latest_revision(first_id) == SceneOutputRevision(3),
        "A successful release must publish a retained revision tombstone");
    const SceneOutputResourceRef minimized = cache.find(SceneOutputId(2));
    check(minimized != nullptr && minimized->extent == SceneOutputExtent{} &&
            minimized->texture == nullptr &&
            minimized->render_target_view == nullptr &&
            minimized->shader_resource_view == nullptr,
        "A zero-extent live output must publish metadata without native resources");

    apply = cache.apply_updates({make_update(1, 4, {64, 64})});
    check(!apply.succeeded() && cache.is_released(first_id),
        "A released SceneOutputId must remain terminal even at a newer revision");
    SceneOutputUpdate duplicate_release = make_update(1, 4, {});
    duplicate_release.operation = SceneOutputUpdateOperation::Release;
    apply = cache.apply_updates({duplicate_release});
    check(!apply.succeeded() && cache.is_released(first_id),
        "A duplicate release must not erase or reopen its tombstone");

    const std::vector<RHIErrorCode> frame_codes = {
        RHIErrorCode::InvalidArgument,
        RHIErrorCode::Unsupported,
        RHIErrorCode::OutOfMemory};
    for (RHIErrorCode code : frame_codes)
    {
        const RenderFrameExecutionStatus status =
            scene_output_apply_execution_status(
                RHIStatus::failure(code, "frame failure"));
        check(status.outcome == RenderFrameExecutionOutcome::FrameFailed &&
                status.rhi_error_code == code,
            "Recoverable SceneOutput apply errors must remain structured frame failures");
    }
    const std::vector<RHIErrorCode> fatal_codes = {
        RHIErrorCode::NotReady,
        RHIErrorCode::OutOfDate,
        RHIErrorCode::Suboptimal,
        RHIErrorCode::DeviceLost,
        RHIErrorCode::BackendFailure};
    for (RHIErrorCode code : fatal_codes)
    {
        const RenderFrameExecutionStatus status =
            scene_output_apply_execution_status(
                RHIStatus::failure(code, "fatal failure"));
        check(status.outcome == RenderFrameExecutionOutcome::FatalRenderer &&
                status.rhi_error_code == code,
            "Terminal or viewport-only SceneOutput apply errors must remain structured fatal failures");
    }
    check(static_cast<bool>(
            scene_output_apply_execution_status(RHIStatus::success())),
        "A successful SceneOutput apply status must remain successful");

    if (failure_count != 0)
    {
        std::cerr << failure_count <<
            " SceneOutput resource cache check(s) failed.\n";
        return 1;
    }
    std::cout << "SceneOutput resource cache checks passed.\n";
    return 0;
}
