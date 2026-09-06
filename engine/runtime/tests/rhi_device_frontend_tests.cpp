#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_queue.h"

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

    class RecordingQueue final : public toy3d::RHIQueue
    {
    public:
        toy3d::RHIQueueCompletionValue completed_value() const override { return 0; }
        toy3d::RHIStatus wait_for_value(toy3d::RHIQueueCompletionValue) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus wait_idle() override { return toy3d::RHIStatus::success(); }

    protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(
            const toy3d::RHISubmitInfo&) override
        {
            return toy3d::RHIResult<toy3d::RHISubmitResult>::success({1});
        }
    };

    class RecordingFence final : public toy3d::RHIGPUFence
    {
    public:
        RecordingFence(const toy3d::RHIDevice& owner, std::string debug_name)
            : RHIGPUFence(owner, std::move(debug_name))
        {
        }

        toy3d::RHIResult<bool> is_signaled() const override
        {
            return toy3d::RHIResult<bool>::success(false);
        }
    };

    class RecordingContext final : public toy3d::RHIGraphicsCommandContext
    {
    public:
        explicit RecordingContext(const toy3d::RHIDevice& owner)
            : RHIGraphicsCommandContext(owner)
        {
        }

        toy3d::RHIStatus begin_recording(const std::string&) override { return unsupported(); }
        toy3d::RHIStatus transition_resources(
            const std::vector<toy3d::RHIResourceTransition>&) override { return unsupported(); }
        toy3d::RHIStatus copy_buffer(const toy3d::RHIBufferCopyDesc&) override { return unsupported(); }
        toy3d::RHIStatus upload_buffer(const toy3d::RHIBufferUploadDesc&) override { return unsupported(); }
        toy3d::RHIStatus copy_texture(const toy3d::RHITextureCopyDesc&) override { return unsupported(); }
        toy3d::RHIStatus upload_texture(const toy3d::RHITextureUploadDesc&) override { return unsupported(); }
        toy3d::RHIStatus write_gpu_fence(const toy3d::RHIGPUFenceRef&) override { return unsupported(); }
        toy3d::RHIResult<toy3d::RHICommandListRef> finish_recording() override
        {
            return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Recording fake does not execute commands.");
        }
        toy3d::RHIStatus begin_render_pass(const toy3d::RHIRenderPassDesc&) override { return unsupported(); }
        toy3d::RHIStatus end_render_pass() override { return unsupported(); }
        toy3d::RHIStatus set_graphics_pipeline(
            const toy3d::RHIGraphicsPipelineRef&) override { return unsupported(); }
        toy3d::RHIStatus set_viewport(const toy3d::RHIViewport&) override { return unsupported(); }
        toy3d::RHIStatus set_scissor(const toy3d::RHIRect&) override { return unsupported(); }
        toy3d::RHIStatus set_blend_constants(const toy3d::vec4&) override { return unsupported(); }
        toy3d::RHIStatus set_stencil_reference(std::uint8_t) override { return unsupported(); }
        toy3d::RHIStatus set_vertex_buffers(
            const std::vector<toy3d::RHIVertexBufferBinding>&) override { return unsupported(); }
        toy3d::RHIStatus set_index_buffer(
            const toy3d::RHIIndexBufferBinding&) override { return unsupported(); }
        toy3d::RHIStatus draw(const toy3d::RHIDrawArgs&) override { return unsupported(); }
        toy3d::RHIStatus draw_indexed(const toy3d::RHIDrawIndexedArgs&) override { return unsupported(); }

    protected:
        toy3d::RHIStatus bind_graphics_bindings_impl(
            const toy3d::RHIGraphicsBindings&) override { return unsupported(); }

    private:
        static toy3d::RHIStatus unsupported()
        {
            return toy3d::RHIStatus::failure(
                toy3d::RHIErrorCode::Unsupported,
                "Recording fake does not execute commands.");
        }
    };

    class RecordingViewport final : public toy3d::RHIViewportContext
    {
    public:
        RecordingViewport(const toy3d::RHIDevice& owner, std::string debug_name)
            : RHIViewportContext(owner, std::move(debug_name))
        {
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>> begin_frame() override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIFrameContext>>::failure(
                toy3d::RHIErrorCode::Unsupported, "Recording fake has no presentation frames.");
        }
        toy3d::RHIResult<toy3d::RHIFrameEndResult> end_frame(
            std::unique_ptr<toy3d::RHIFrameContext>,
            const std::vector<toy3d::RHICommandListRef>&) override
        {
            return toy3d::RHIResult<toy3d::RHIFrameEndResult>::failure(
                toy3d::RHIErrorCode::Unsupported, "Recording fake has no presentation frames.");
        }
        toy3d::RHIStatus abort_frame(std::unique_ptr<toy3d::RHIFrameContext>) override
        {
            return toy3d::RHIStatus::failure(
                toy3d::RHIErrorCode::Unsupported, "Recording fake has no presentation frames.");
        }
        toy3d::RHIStatus request_resize(std::uint32_t, std::uint32_t) override
        {
            return toy3d::RHIStatus::failure(
                toy3d::RHIErrorCode::Unsupported, "Recording fake has no presentation frames.");
        }
    };

    struct HookCounts
    {
        int viewport = 0;
        int buffer = 0;
        int texture = 0;
        int buffer_view = 0;
        int texture_view = 0;
        int shader = 0;
        int binding_layout = 0;
        int sampler = 0;
        int binding_set = 0;
        int pipeline = 0;
        int fence = 0;
        int context = 0;
    };

    class RecordingDevice final : public toy3d::RHIDevice
    {
    public:
        RecordingDevice()
        {
            device_capabilities.compute_dispatch = true;
            device_capabilities.storage_resources = true;
            device_capabilities.indirect_draw = true;
            device_capabilities.geometry_shader = true;
            device_capabilities.tessellation_shader = true;
            device_limits.max_color_attachments = toy3d::RHI_MAX_COLOR_ATTACHMENTS;
            device_limits.max_vertex_buffers = 16;
            device_limits.max_texture_dimension_2d = 16384;
            device_limits.max_texture_array_layers = 2048;
            device_limits.max_uniform_buffer_size = 65536;
            device_limits.max_binding_slots_per_group = 64;
            device_limits.max_sampler_anisotropy = 16;
            device_limits.uniform_buffer_offset_alignment = 16;
        }

        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override
        {
            initialized = true;
            return toy3d::RHIStatus::success();
        }
        const toy3d::RHICapabilities& capabilities() const override { return device_capabilities; }
        const toy3d::RHILimits& limits() const override { return device_limits; }
        toy3d::RHIFormatCapabilities format_capabilities(toy3d::PixelFormat format) const override
        {
            if (format == toy3d::PixelFormat::Unknown)
            {
                return {};
            }
            toy3d::RHIFormatCapabilities result;
            result.usage = toy3d::RHIFormatUsage::Sampled |
                toy3d::RHIFormatUsage::Storage |
                toy3d::RHIFormatUsage::RenderTarget |
                toy3d::RHIFormatUsage::DepthStencil |
                toy3d::RHIFormatUsage::VertexBuffer |
                toy3d::RHIFormatUsage::CopySource |
                toy3d::RHIFormatUsage::CopyDestination;
            result.supported_sample_counts = 1;
            return result;
        }
        toy3d::RHIQueue& graphics_queue() override { return queue; }

        HookCounts counts;
        bool fail_next_buffer = false;
        bool lose_device_on_next_buffer = false;
        bool wrong_buffer_owner = false;
        bool wrong_texture_view_owner = false;
        bool wrong_shader_owner = false;
        bool fence_unsupported = false;
        bool context_unsupported = false;
        int wait_idle_count = 0;
        int shutdown_count = 0;

        void block_next_buffer_creation()
        {
            std::lock_guard<std::mutex> lock(block_mutex);
            block_buffer = true;
            release_buffer = false;
            buffer_entered = false;
        }

        void wait_until_buffer_hook_entered()
        {
            std::unique_lock<std::mutex> lock(block_mutex);
            block_changed.wait(lock, [this]() { return buffer_entered; });
        }

        void release_blocked_buffer()
        {
            {
                std::lock_guard<std::mutex> lock(block_mutex);
                release_buffer = true;
            }
            block_changed.notify_all();
        }

    protected:
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>
            create_viewport_context_impl(
                const toy3d::RHISurfaceRef&,
                const toy3d::RHIViewportContextDesc& desc) override
        {
            ++counts.viewport;
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>::success(
                std::make_unique<RecordingViewport>(*this, desc.debug_name));
        }

        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer_impl(
            const toy3d::RHIBufferDesc& desc,
            const toy3d::RHIInitialData*) override
        {
            ++counts.buffer;
            {
                std::unique_lock<std::mutex> lock(block_mutex);
                if (block_buffer)
                {
                    buffer_entered = true;
                    block_changed.notify_all();
                    block_changed.wait(lock, [this]() { return release_buffer; });
                    block_buffer = false;
                }
            }
            if (fail_next_buffer)
            {
                fail_next_buffer = false;
                return toy3d::RHIResult<toy3d::RHIBufferRef>::failure(
                    toy3d::RHIErrorCode::OutOfMemory, "Injected buffer failure.");
            }
            if (lose_device_on_next_buffer)
            {
                lose_device_on_next_buffer = false;
                return toy3d::RHIResult<toy3d::RHIBufferRef>::failure(
                    toy3d::RHIErrorCode::DeviceLost, "Injected device loss.");
            }
            const toy3d::RHIDevice& owner = wrong_buffer_owner && alternate_owner != nullptr
                ? *alternate_owner
                : *this;
            return toy3d::RHIResult<toy3d::RHIBufferRef>::success(
                std::make_shared<toy3d::RHIBuffer>(owner, desc));
        }

        toy3d::RHIResult<toy3d::RHITextureRef> create_texture_impl(
            const toy3d::RHITextureDesc& desc,
            const toy3d::RHIInitialData*) override
        {
            ++counts.texture;
            return toy3d::RHIResult<toy3d::RHITextureRef>::success(
                std::make_shared<toy3d::RHITexture>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view_impl(
            const toy3d::RHIBufferRef& buffer,
            const toy3d::RHIBufferViewDesc& desc) override
        {
            ++counts.buffer_view;
            return toy3d::RHIResult<toy3d::RHIBufferViewRef>::success(
                std::make_shared<toy3d::RHIBufferView>(buffer, desc));
        }

        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view_impl(
            const toy3d::RHITextureRef& texture,
            const toy3d::RHITextureViewDesc& desc) override
        {
            ++counts.texture_view;
            if (wrong_texture_view_owner && alternate_owner != nullptr)
            {
                const auto foreign_texture = std::make_shared<toy3d::RHITexture>(
                    *alternate_owner, texture->desc());
                return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                    std::make_shared<toy3d::RHITextureView>(foreign_texture, desc));
            }
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                std::make_shared<toy3d::RHITextureView>(texture, desc));
        }

        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(
            const toy3d::RHIShaderDesc& desc) override
        {
            ++counts.shader;
            const toy3d::RHIDevice& owner = wrong_shader_owner && alternate_owner != nullptr
                ? *alternate_owner
                : *this;
            return toy3d::RHIResult<toy3d::RHIShaderRef>::success(
                std::make_shared<toy3d::RHIShader>(owner, desc));
        }

        toy3d::RHIResult<toy3d::RHIBindingLayoutRef> create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc& desc) override
        {
            ++counts.binding_layout;
            return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::success(
                std::make_shared<toy3d::RHIBindingLayout>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler_impl(
            const toy3d::RHISamplerDesc& desc) override
        {
            ++counts.sampler;
            return toy3d::RHIResult<toy3d::RHISamplerRef>::success(
                std::make_shared<toy3d::RHISampler>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIBindingSetRef> create_binding_set_impl(
            const toy3d::RHIBindingSetDesc& desc) override
        {
            ++counts.binding_set;
            return toy3d::RHIResult<toy3d::RHIBindingSetRef>::success(
                std::make_shared<toy3d::RHIBindingSet>(desc));
        }

        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc& desc) override
        {
            ++counts.pipeline;
            return toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>::success(
                std::make_shared<toy3d::RHIGraphicsPipeline>(*this, desc));
        }

        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence_impl(
            const std::string& debug_name) override
        {
            ++counts.fence;
            if (fence_unsupported)
            {
                return toy3d::RHIResult<toy3d::RHIGPUFenceRef>::failure(
                    toy3d::RHIErrorCode::Unsupported, "Injected unsupported fence.");
            }
            return toy3d::RHIResult<toy3d::RHIGPUFenceRef>::success(
                std::make_shared<RecordingFence>(*this, debug_name));
        }

        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>
            create_graphics_command_context_impl() override
        {
            ++counts.context;
            if (context_unsupported)
            {
                return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::failure(
                    toy3d::RHIErrorCode::Unsupported,
                    "Injected unsupported graphics context.");
            }
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::success(
                std::make_unique<RecordingContext>(*this));
        }

        bool is_initialized_impl() const override { return initialized; }
        toy3d::RHIStatus wait_idle_before_shutdown_impl() override
        {
            ++wait_idle_count;
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus shutdown_impl() override
        {
            ++shutdown_count;
            initialized = false;
            return toy3d::RHIStatus::success();
        }

    public:
        const toy3d::RHIDevice* alternate_owner = nullptr;

    private:
        RecordingQueue queue;
        toy3d::RHICapabilities device_capabilities;
        toy3d::RHILimits device_limits;
        bool initialized = false;
        std::mutex block_mutex;
        std::condition_variable block_changed;
        bool block_buffer = false;
        bool buffer_entered = false;
        bool release_buffer = false;
    };

    toy3d::RHIShaderDesc make_shader_desc(toy3d::RHIShaderStage stage, std::uint8_t seed)
    {
        toy3d::RHIShaderDesc desc;
        desc.stage = stage;
        desc.bytecode.bytes = {seed, 0, 0, 0};
        desc.bytecode.target = "test";
        desc.content_hash = {seed, 0};
        desc.debug_name = "frontend shader";
        return desc;
    }

    toy3d::RHISurfaceRef make_surface()
    {
        static int fake_window_identity = 0;
        toy3d::RHISurfaceDesc desc;
        desc.platform = toy3d::RHISurfacePlatform::Glfw;
        desc.window_handle = &fake_window_identity;
        desc.debug_name = "frontend surface";
        return std::make_shared<toy3d::RHISurface>(desc);
    }

    void initialize(RecordingDevice& device)
    {
        check(static_cast<bool>(device.initialize({})), "recording device must initialize");
    }

    void test_legal_creation_calls_each_hook_once()
    {
        RecordingDevice device;
        initialize(device);

        toy3d::RHIBufferDesc buffer_desc;
        buffer_desc.size = 256;
        buffer_desc.usage = toy3d::RHIResourceUsage::ShaderResource |
            toy3d::RHIResourceUsage::UniformBuffer;
        const auto buffer = device.create_buffer(buffer_desc);
        check(static_cast<bool>(buffer), "legal buffer must be created");

        toy3d::RHITextureDesc texture_desc;
        texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
        texture_desc.usage = toy3d::RHIResourceUsage::ShaderResource;
        const auto texture = device.create_texture(texture_desc);
        check(static_cast<bool>(texture), "legal texture must be created");

        toy3d::RHIBufferViewDesc buffer_view_desc;
        buffer_view_desc.size = buffer_desc.size;
        const auto buffer_view = device.create_buffer_view(buffer.value(), buffer_view_desc);
        check(static_cast<bool>(buffer_view), "legal buffer view must be created");

        toy3d::RHITextureViewDesc texture_view_desc;
        texture_view_desc.format = texture_desc.format;
        const auto texture_view = device.create_texture_view(texture.value(), texture_view_desc);
        check(static_cast<bool>(texture_view), "legal texture view must be created");

        const auto vertex_shader = device.create_shader(
            make_shader_desc(toy3d::RHIShaderStage::Vertex, 1));
        const auto pixel_shader = device.create_shader(
            make_shader_desc(toy3d::RHIShaderStage::Pixel, 2));
        check(vertex_shader && pixel_shader, "legal shaders must be created");

        const auto layout = device.create_binding_layout({});
        check(static_cast<bool>(layout), "legal binding layout must be created");
        const auto sampler = device.create_sampler({});
        check(static_cast<bool>(sampler), "legal sampler must be created");

        toy3d::RHIBindingSetDesc set_desc;
        set_desc.layout = layout.value();
        const auto binding_set = device.create_binding_set(set_desc);
        check(static_cast<bool>(binding_set), "legal binding set must be created");

        toy3d::RHIGraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = vertex_shader.value();
        pipeline_desc.pixel_shader = pixel_shader.value();
        pipeline_desc.binding_layout = layout.value();
        pipeline_desc.color_attachment_count = 1;
        pipeline_desc.color_formats[0] = toy3d::PixelFormat::B8G8R8A8UNorm;
        const auto pipeline = device.create_graphics_pipeline(pipeline_desc);
        check(static_cast<bool>(pipeline), "legal graphics pipeline must be created");

        const auto fence = device.create_gpu_fence("frontend fence");
        check(static_cast<bool>(fence), "legal GPU fence must be created");
        const auto context = device.create_graphics_command_context();
        check(static_cast<bool>(context), "legal command context must be created");

        toy3d::RHIViewportContextDesc viewport_desc;
        viewport_desc.debug_name = "frontend viewport";
        const auto viewport = device.create_viewport_context(make_surface(), viewport_desc);
        check(static_cast<bool>(viewport), "legal viewport must be created");

        check(device.counts.buffer == 1 && device.counts.texture == 1 &&
              device.counts.buffer_view == 1 && device.counts.texture_view == 1 &&
              device.counts.shader == 2 && device.counts.binding_layout == 1 &&
              device.counts.sampler == 1 && device.counts.binding_set == 1 &&
              device.counts.pipeline == 1 && device.counts.fence == 1 &&
              device.counts.context == 1 && device.counts.viewport == 1,
            "each legal frontend path must call its backend hook exactly once");
    }

    void test_frontend_rejects_invalid_and_cross_device_inputs()
    {
        RecordingDevice first;
        RecordingDevice second;
        initialize(first);
        initialize(second);

        toy3d::RHIBufferDesc invalid_buffer;
        check(!first.create_buffer(invalid_buffer) && first.counts.buffer == 0,
            "invalid buffer descriptor must not enter backend");

        toy3d::RHITextureDesc texture_desc;
        texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
        texture_desc.usage = toy3d::RHIResourceUsage::ShaderResource;
        const auto foreign_texture = first.create_texture(texture_desc);
        toy3d::RHITextureViewDesc view_desc;
        view_desc.format = texture_desc.format;
        const auto cross_device_view = second.create_texture_view(foreign_texture.value(), view_desc);
        check(!cross_device_view &&
              cross_device_view.status().code() == toy3d::RHIErrorCode::InvalidArgument &&
              second.counts.texture_view == 0,
            "cross-device texture view must fail before backend");

        const auto foreign_layout = first.create_binding_layout({});
        toy3d::RHIBindingSetDesc set_desc;
        set_desc.layout = foreign_layout.value();
        const auto cross_device_set = second.create_binding_set(set_desc);
        check(!cross_device_set && second.counts.binding_set == 0,
            "cross-device binding layout must fail before backend");

        check(!second.create_gpu_fence("") && second.counts.fence == 0,
            "empty GPU fence name must fail before backend");
    }

    void test_backend_contract_and_unsupported_results()
    {
        RecordingDevice first;
        RecordingDevice second;
        initialize(first);
        initialize(second);
        first.wrong_shader_owner = true;
        first.alternate_owner = &second;
        const auto wrong_owner = first.create_shader(
            make_shader_desc(toy3d::RHIShaderStage::Vertex, 3));
        check(!wrong_owner && wrong_owner.status().code() == toy3d::RHIErrorCode::BackendFailure,
            "backend result with wrong owner must become BackendFailure");

        toy3d::RHIBufferDesc buffer_desc;
        buffer_desc.size = 16;
        buffer_desc.usage = toy3d::RHIResourceUsage::VertexBuffer;
        first.wrong_buffer_owner = true;
        const auto wrong_buffer_owner = first.create_buffer(buffer_desc);
        check(!wrong_buffer_owner &&
              wrong_buffer_owner.status().code() == toy3d::RHIErrorCode::BackendFailure,
            "wrong resource owner must become BackendFailure");

        toy3d::RHITextureDesc texture_desc;
        texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
        texture_desc.usage = toy3d::RHIResourceUsage::ShaderResource;
        const auto texture = first.create_texture(texture_desc);
        toy3d::RHITextureViewDesc view_desc;
        view_desc.format = texture_desc.format;
        first.wrong_texture_view_owner = true;
        const auto wrong_view_owner = first.create_texture_view(texture.value(), view_desc);
        check(!wrong_view_owner &&
              wrong_view_owner.status().code() == toy3d::RHIErrorCode::BackendFailure,
            "wrong view owner must become BackendFailure");

        first.fence_unsupported = true;
        const auto unsupported = first.create_gpu_fence("unsupported fence");
        check(!unsupported && unsupported.status().code() == toy3d::RHIErrorCode::Unsupported,
            "backend Unsupported result must be preserved");

        first.context_unsupported = true;
        const auto unsupported_context = first.create_graphics_command_context();
        check(!unsupported_context &&
              unsupported_context.status().code() == toy3d::RHIErrorCode::Unsupported,
            "unsupported graphics context result must be preserved");
    }

    void test_admission_and_shutdown_race()
    {
        RecordingDevice device;
        initialize(device);
        toy3d::RHIBufferDesc desc;
        desc.size = 16;
        desc.usage = toy3d::RHIResourceUsage::VertexBuffer;

        device.block_next_buffer_creation();
        bool create_succeeded = false;
        std::thread create_thread([&device, &desc, &create_succeeded]()
        {
            create_succeeded = static_cast<bool>(device.create_buffer(desc));
        });
        device.wait_until_buffer_hook_entered();

        bool shutdown_succeeded = false;
        std::thread shutdown_thread([&device, &shutdown_succeeded]()
        {
            shutdown_succeeded = static_cast<bool>(device.shutdown());
        });

        auto rejected = device.create_sampler({});
        while (rejected)
        {
            std::this_thread::yield();
            rejected = device.create_sampler({});
        }
        const int sampler_hooks_before_second_rejection = device.counts.sampler;
        const auto rejected_again = device.create_sampler({});
        check(rejected.status().code() == toy3d::RHIErrorCode::NotReady &&
              !rejected_again &&
              device.counts.sampler == sampler_hooks_before_second_rejection &&
              device.shutdown_count == 0,
            "shutdown must reject new creation and wait before backend teardown");
        device.release_blocked_buffer();
        create_thread.join();
        shutdown_thread.join();
        check(create_succeeded,
            "admitted buffer creation must finish successfully");
        check(shutdown_succeeded,
            "shutdown must succeed after admitted creation finishes");
        check(device.shutdown_count == 1 && device.wait_idle_count == 1,
            "ordinary shutdown must wait idle and tear down once");

        RecordingDevice failing_device;
        initialize(failing_device);
        failing_device.fail_next_buffer = true;
        check(!failing_device.create_buffer(desc), "injected backend failure must propagate");
        check(static_cast<bool>(failing_device.shutdown()),
            "backend failure must release creation admission");

        RecordingDevice frontend_failure_device;
        initialize(frontend_failure_device);
        toy3d::RHISamplerDesc unsupported_sampler;
        unsupported_sampler.max_anisotropy = 32;
        check(!frontend_failure_device.create_sampler(unsupported_sampler) &&
              frontend_failure_device.counts.sampler == 0,
            "frontend capability failure must not enter backend");
        check(static_cast<bool>(frontend_failure_device.shutdown()),
            "frontend failure must release creation admission");

        RecordingDevice uninitialized_device;
        check(!uninitialized_device.create_graphics_command_context() &&
              uninitialized_device.counts.context == 0,
            "uninitialized device must reject creation before backend");

        RecordingDevice lost_device;
        initialize(lost_device);
        lost_device.lose_device_on_next_buffer = true;
        const auto device_lost_result = lost_device.create_buffer(desc);
        check(!device_lost_result &&
              device_lost_result.status().code() == toy3d::RHIErrorCode::DeviceLost,
            "backend DeviceLost must propagate through the frontend");
        const auto rejected_after_loss = lost_device.create_sampler({});
        check(!rejected_after_loss &&
              rejected_after_loss.status().code() == toy3d::RHIErrorCode::DeviceLost &&
              lost_device.counts.sampler == 0,
            "backend DeviceLost must latch the terminal creation state");
        check(static_cast<bool>(lost_device.shutdown_after_device_lost()),
            "device-lost shutdown must complete");
        check(lost_device.wait_idle_count == 0 && lost_device.shutdown_count == 1,
            "device-lost shutdown must share admission without native idle wait");
        const auto after_device_lost = lost_device.create_graphics_command_context();
        check(!after_device_lost &&
              after_device_lost.status().code() == toy3d::RHIErrorCode::DeviceLost &&
              lost_device.counts.context == 0,
            "creation after shutdown must not enter backend");
    }
}

int main()
{
    test_legal_creation_calls_each_hook_once();
    test_frontend_rejects_invalid_and_cross_device_inputs();
    test_backend_contract_and_unsupported_results();
    test_admission_and_shutdown_race();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI device frontend tests passed\n";
    return 0;
}
