#include "drivers/rhi/rhi_queue.h"
#include "renderscene/resources/render_resource_cache.h"

#include <cstdint>
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

    toy3d::TextureRenderResourceVersionRef make_placeholder_texture(
        std::uint64_t id,
        toy3d::TextureColorSemantic semantic)
    {
        auto texture = std::make_shared<toy3d::TextureRenderResourceVersion>();
        texture->resource_id = toy3d::TextureRenderResourceId(id);
        texture->revision = toy3d::RenderResourceRevision(1);
        texture->width = 1;
        texture->height = 1;
        texture->color_semantic = semantic;
        texture->rgba8_pixels = {255, 255, 255, 255};
        return texture;
    }

    toy3d::RenderResourcePlaceholders make_placeholders()
    {
        auto material = std::make_shared<toy3d::MaterialRenderResourceVersion>();
        material->resource_id = toy3d::MaterialRenderResourceId(9001);
        material->revision = toy3d::RenderResourceRevision(1);
        material->material.shader_name = "Builtin/Error";

        toy3d::RenderResourcePlaceholders placeholders;
        placeholders.error_material = std::move(material);
        placeholders.checkerboard_texture = make_placeholder_texture(
            9002, toy3d::TextureColorSemantic::Color);
        placeholders.white_texture = make_placeholder_texture(
            9003, toy3d::TextureColorSemantic::Linear);
        placeholders.normal_texture = make_placeholder_texture(
            9004, toy3d::TextureColorSemantic::Normal);
        return placeholders;
    }

    toy3d::MeshRenderResourceVersionRef make_mesh_version(
        std::uint64_t id,
        std::uint64_t revision,
        float position_offset = 0.0F)
    {
        auto mesh = std::make_shared<toy3d::MeshRenderResourceVersion>();
        mesh->resource_id = toy3d::MeshRenderResourceId(id);
        mesh->revision = toy3d::RenderResourceRevision(revision);
        mesh->vertices = {
            {{-1.0F + position_offset, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}},
            {{1.0F + position_offset, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
            {{position_offset, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.5F, 1.0F}}};
        mesh->indices = std::vector<std::uint16_t>{0, 1, 2};
        mesh->sections.push_back({0, 3, 0});
        return mesh;
    }

    toy3d::RenderResourceUpdate mesh_update(
        const toy3d::MeshRenderResourceVersionRef& version)
    {
        toy3d::MeshRenderResourceUpdate update;
        update.resource_id = version->resource_id;
        update.version = version;
        return update;
    }

    class FakeBuffer final : public toy3d::RHIBuffer
    {
    public:
        explicit FakeBuffer(toy3d::RHIBufferDesc desc)
            : RHIBuffer(std::move(desc))
        {
        }
    };

    class FakeTexture final : public toy3d::RHITexture
    {
    public:
        explicit FakeTexture(toy3d::RHITextureDesc desc)
            : RHITexture(std::move(desc))
        {
        }
    };

    class FakeTextureView final : public toy3d::RHITextureView
    {
    public:
        FakeTextureView(
            toy3d::RHITextureRef texture,
            toy3d::RHITextureViewDesc desc)
            : RHITextureView(std::move(texture), std::move(desc))
        {
        }
    };

    class FakeQueue final : public toy3d::RHIQueue
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
            return toy3d::RHIResult<toy3d::RHISubmitResult>::failure(
                toy3d::RHIErrorCode::Unsupported,
                "The upload unit test does not submit command lists.");
        }
    };

    class FakeDevice final : public toy3d::RHIDevice
    {
    public:
        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override
        {
            initialized = true;
            return toy3d::RHIStatus::success();
        }
        const toy3d::RHICapabilities& capabilities() const override { return capabilities_; }
        const toy3d::RHILimits& limits() const override { return limits_; }
        toy3d::RHIFormatCapabilities format_capabilities(toy3d::RHIFormat) const override
        {
            return {};
        }
        toy3d::RHIQueue& graphics_queue() override { return queue; }
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>
        create_viewport_context(
            const toy3d::RHISurfaceRef&,
            const toy3d::RHIViewportContextDesc&) override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>::failure(
                toy3d::RHIErrorCode::Unsupported, "Not required by this test.");
        }
        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer(
            const toy3d::RHIBufferDesc& desc,
            const toy3d::RHIInitialData* initial_data = nullptr) override
        {
            if (initial_data != nullptr)
            {
                return toy3d::RHIResult<toy3d::RHIBufferRef>::failure(
                    toy3d::RHIErrorCode::InvalidArgument,
                    "Mesh upload buffers must not use creation-time data.");
            }
            created_buffer_descs.push_back(desc);
            return toy3d::RHIResult<toy3d::RHIBufferRef>::success(
                std::make_shared<FakeBuffer>(desc));
        }
        toy3d::RHIResult<toy3d::RHITextureRef> create_texture(
            const toy3d::RHITextureDesc& desc,
            const toy3d::RHIInitialData* initial_data = nullptr) override
        {
            if (initial_data != nullptr)
            {
                return toy3d::RHIResult<toy3d::RHITextureRef>::failure(
                    toy3d::RHIErrorCode::InvalidArgument,
                    "Texture uploads must not use creation-time data.");
            }
            created_texture_descs.push_back(desc);
            return toy3d::RHIResult<toy3d::RHITextureRef>::success(
                std::make_shared<FakeTexture>(desc));
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
            created_texture_view_descs.push_back(desc);
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::success(
                std::make_shared<FakeTextureView>(texture, desc));
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
            return unsupported<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>();
        }

        std::vector<toy3d::RHIBufferDesc> created_buffer_descs;
        std::vector<toy3d::RHITextureDesc> created_texture_descs;
        std::vector<toy3d::RHITextureViewDesc> created_texture_view_descs;

    protected:
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(
            const toy3d::RHIShaderDesc&) override
        {
            return unsupported<toy3d::RHIShaderRef>();
        }
        toy3d::RHIResult<toy3d::RHIBindingLayoutRef> create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc&) override
        {
            return unsupported<toy3d::RHIBindingLayoutRef>();
        }
        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc&) override
        {
            return unsupported<toy3d::RHIGraphicsPipelineRef>();
        }
        bool is_initialized_impl() const override { return initialized; }
        toy3d::RHIStatus wait_idle_before_shutdown_impl() override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus shutdown_impl() override
        {
            initialized = false;
            return toy3d::RHIStatus::success();
        }

    private:
        template<typename T>
        static toy3d::RHIResult<T> unsupported()
        {
            return toy3d::RHIResult<T>::failure(
                toy3d::RHIErrorCode::Unsupported, "Not required by this test.");
        }

        bool initialized = true;
        toy3d::RHICapabilities capabilities_;
        toy3d::RHILimits limits_;
        FakeQueue queue;
    };

    class FakeGraphicsCommandContext final : public toy3d::RHIGraphicsCommandContext
    {
    public:
        enum class OperationType
        {
            Transition,
            Upload
        };

        struct Operation
        {
            OperationType type = OperationType::Transition;
            toy3d::RHIBufferRef buffer;
            toy3d::RHIAccess before = toy3d::RHIAccess::Unknown;
            toy3d::RHIAccess after = toy3d::RHIAccess::Unknown;
            std::vector<std::uint8_t> bytes;
        };

        toy3d::RHIStatus begin_recording(const std::string&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus transition_resources(
            const std::vector<toy3d::RHIResourceTransition>& transitions) override
        {
            for (const toy3d::RHIResourceTransition& transition : transitions)
            {
                Operation operation;
                operation.type = OperationType::Transition;
                operation.buffer = std::dynamic_pointer_cast<toy3d::RHIBuffer>(
                    transition.resource);
                operation.before = transition.before;
                operation.after = transition.after;
                operations.push_back(std::move(operation));
            }
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus copy_buffer(const toy3d::RHIBufferCopyDesc&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus upload_buffer(
            const toy3d::RHIBufferUploadDesc& desc) override
        {
            ++upload_count;
            if (fail_upload_number != 0 && upload_count == fail_upload_number)
            {
                return toy3d::RHIStatus::failure(
                    toy3d::RHIErrorCode::BackendFailure,
                    "Injected upload recording failure.");
            }
            Operation operation;
            operation.type = OperationType::Upload;
            operation.buffer = desc.destination;
            operation.bytes.resize(desc.source.size);
            std::memcpy(
                operation.bytes.data(), desc.source.data, desc.source.size);
            operations.push_back(std::move(operation));
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus copy_texture(const toy3d::RHITextureCopyDesc&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus upload_texture(
            const toy3d::RHITextureUploadDesc& desc) override
        {
            ++texture_upload_count;
            last_texture_row_pitch = desc.source.row_pitch;
            last_texture_slice_pitch = desc.source.slice_pitch;
            last_texture_extent = desc.extent;
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus write_gpu_fence(const toy3d::RHIGPUFenceRef&) override
        {
            return unsupported();
        }
        toy3d::RHIResult<toy3d::RHICommandListRef> finish_recording() override
        {
            return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Not required by this test.");
        }
        toy3d::RHIStatus begin_render_pass(const toy3d::RHIRenderPassDesc&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus end_render_pass() override { return unsupported(); }
        toy3d::RHIStatus set_graphics_pipeline(
            const toy3d::RHIGraphicsPipelineRef&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus set_viewport(const toy3d::RHIViewport&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus set_scissor(const toy3d::RHIRect&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus set_blend_constants(const toy3d::vec4&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus set_stencil_reference(std::uint8_t) override
        {
            return unsupported();
        }
        toy3d::RHIStatus set_vertex_buffers(
            const std::vector<toy3d::RHIVertexBufferBinding>&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus set_index_buffer(
            const toy3d::RHIIndexBufferBinding&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus draw(const toy3d::RHIDrawArgs&) override
        {
            return unsupported();
        }
        toy3d::RHIStatus draw_indexed(const toy3d::RHIDrawIndexedArgs&) override
        {
            return unsupported();
        }

        std::size_t fail_upload_number = 0;
        std::size_t upload_count = 0;
        std::size_t texture_upload_count = 0;
        std::size_t last_texture_row_pitch = 0;
        std::size_t last_texture_slice_pitch = 0;
        toy3d::RHIExtent3D last_texture_extent;
        std::vector<Operation> operations;

    protected:
        toy3d::RHIStatus bind_graphics_bindings_impl(
            const toy3d::RHIGraphicsBindings&) override
        {
            return unsupported();
        }

    private:
        static toy3d::RHIStatus unsupported()
        {
            return toy3d::RHIStatus::failure(
                toy3d::RHIErrorCode::Unsupported, "Not required by this test.");
        }
    };
}

int main()
{
    using namespace toy3d;

    RenderResourceCache cache(make_placeholders());
    const MeshRenderResourceVersionRef version_one = make_mesh_version(100, 1);
    const RenderResourceApplyResult apply_one =
        cache.apply_updates({mesh_update(version_one)});
    check(apply_one.applied_count == 1,
        "The CPU Mesh version must enter the cache before upload recording");

    FakeDevice device;
    FakeGraphicsCommandContext first_context;
    auto first_result = cache.record_pending_uploads(device, first_context);
    check(first_result && first_result.value().mesh_count() == 1,
        "A new Mesh version must produce one pending upload batch entry");
    check(device.created_buffer_descs.size() == 2 &&
        first_context.operations.size() == 6,
        "Mesh upload must create two GPU-only buffers and record transition/upload/transition for each");
    check(!cache.resolve_mesh_rhi(version_one->resource_id),
        "Recording alone must not publish an unsubmitted Mesh RHI resource");

    RenderResourceUploadBatch discarded_batch = std::move(first_result).value();
    discarded_batch = RenderResourceUploadBatch{};
    FakeGraphicsCommandContext retry_context;
    auto retry_result = cache.record_pending_uploads(device, retry_context);
    check(retry_result && retry_result.value().mesh_count() == 1 &&
        device.created_buffer_descs.size() == 4,
        "Discarding a pre-submit batch must leave the Mesh pending for a fresh retry");
    check(static_cast<bool>(cache.commit_uploads(
            std::move(retry_result).value())),
        "A successfully submitted upload batch must be publishable");

    const auto uploaded_one = cache.resolve_mesh_rhi(version_one->resource_id);
    check(uploaded_one && uploaded_one.version->source_version == version_one &&
        uploaded_one.version->index_format == RHIIndexFormat::UInt16 &&
        uploaded_one.version->vertex_stride == sizeof(StaticMeshVertex),
        "Published Mesh RHI state must retain its exact immutable CPU version and binding ABI");
    FakeGraphicsCommandContext no_work_context;
    auto no_work_result = cache.record_pending_uploads(device, no_work_context);
    check(no_work_result && no_work_result.value().empty() &&
        no_work_context.operations.empty(),
        "An already published latest Mesh version must not upload again");

    const MeshRHIResourceRef retained_old_resource = uploaded_one.version;
    const MeshRenderResourceVersionRef version_two = make_mesh_version(100, 2, 1.0F);
    cache.apply_updates({mesh_update(version_two)});
    check(!cache.resolve_mesh_rhi(version_two->resource_id) &&
        cache.mesh_rhi_count() == 0 && retained_old_resource->source_version == version_one,
        "A newer CPU revision must drop only the cache's old GPU reference while prepared holders stay valid");

    FakeGraphicsCommandContext version_two_context;
    auto version_two_result = cache.record_pending_uploads(
        device, version_two_context);
    const MeshRenderResourceVersionRef version_three = make_mesh_version(100, 3, 2.0F);
    cache.apply_updates({mesh_update(version_three)});
    check(version_two_result &&
        !cache.commit_uploads(std::move(version_two_result).value()),
        "A recorded batch must not overwrite a newer CPU revision at commit time");
    check(cache.mesh_rhi_count() == 0,
        "A rejected stale upload commit must not partially publish GPU state");

    FakeGraphicsCommandContext failing_context;
    failing_context.fail_upload_number = 2;
    auto failing_result = cache.record_pending_uploads(device, failing_context);
    check(!failing_result && cache.mesh_rhi_count() == 0,
        "A recording failure must leave the current Mesh revision pending and unpublished");
    FakeGraphicsCommandContext final_context;
    auto final_result = cache.record_pending_uploads(device, final_context);
    check(final_result && cache.commit_uploads(std::move(final_result).value()) &&
        cache.resolve_mesh_rhi(version_three->resource_id),
        "The next frame must be able to retry and publish after an earlier recording failure");

    MeshRenderResourceUpdate release;
    release.operation = RenderResourceUpdateOperation::Release;
    release.resource_id = version_three->resource_id;
    const RenderResourceApplyResult release_result =
        cache.apply_updates({RenderResourceUpdate(release)});
    check(release_result.released_count == 1 &&
        !cache.resolve_mesh_rhi(version_three->resource_id) &&
        cache.mesh_rhi_count() == 0,
        "Release must remove the cache-owned latest CPU and GPU Mesh versions together");

    const TextureRenderResourceVersionRef color_texture =
        make_placeholder_texture(200, TextureColorSemantic::Color);
    TextureRenderResourceUpdate texture_update;
    texture_update.resource_id = color_texture->resource_id;
    texture_update.version = color_texture;
    check(cache.apply_updates({RenderResourceUpdate(texture_update)}).applied_count == 1,
        "A valid Texture CPU version must enter the cache before upload");
    FakeGraphicsCommandContext texture_context;
    auto texture_result = cache.record_pending_uploads(device, texture_context);
    check(texture_result && texture_result.value().mesh_count() == 0 &&
        texture_result.value().texture_count() == 1,
        "A pending Texture must join the same submission transaction without Mesh work");
    check(device.created_texture_descs.size() == 1 &&
        device.created_texture_descs.back().format == RHIFormat::R8G8B8A8UNormSRGB &&
        device.created_texture_view_descs.size() == 1,
        "Color-semantic Texture upload must create an sRGB texture and SRV");
    check(texture_context.texture_upload_count == 1 &&
        texture_context.last_texture_row_pitch == 4 &&
        texture_context.last_texture_slice_pitch == 4 &&
        texture_context.last_texture_extent.width == 1 &&
        texture_context.operations.size() == 2 &&
        texture_context.operations.front().after == RHIAccess::CopyDestination &&
        texture_context.operations.back().after == RHIAccess::ShaderResourceGraphics,
        "Texture upload must preserve pitches and record copy then graphics-sampling transitions");
    check(!cache.resolve_texture_rhi(color_texture->resource_id),
        "A recorded Texture must remain invisible until submission commit");
    check(static_cast<bool>(cache.commit_uploads(std::move(texture_result).value())) &&
        cache.resolve_texture_rhi(color_texture->resource_id),
        "A committed Texture upload must publish its exact latest RHI resource");

    auto normal_texture_mutable = std::make_shared<TextureRenderResourceVersion>(
        *color_texture);
    normal_texture_mutable->revision = RenderResourceRevision(2);
    normal_texture_mutable->color_semantic = TextureColorSemantic::Normal;
    TextureRenderResourceVersionRef normal_texture =
        std::move(normal_texture_mutable);
    texture_update.version = normal_texture;
    cache.apply_updates({RenderResourceUpdate(texture_update)});
    check(cache.texture_rhi_count() == 0,
        "A new Texture revision must release the cache-owned old RHI version");
    FakeGraphicsCommandContext normal_context;
    auto normal_result = cache.record_pending_uploads(device, normal_context);
    check(normal_result &&
        device.created_texture_descs.back().format == RHIFormat::R8G8B8A8UNorm &&
        cache.commit_uploads(std::move(normal_result).value()),
        "Normal-semantic Texture data must use a linear UNorm RHI resource");

    TextureRenderResourceUpdate texture_release;
    texture_release.operation = RenderResourceUpdateOperation::Release;
    texture_release.resource_id = normal_texture->resource_id;
    check(cache.apply_updates({RenderResourceUpdate(texture_release)}).released_count == 1 &&
        cache.texture_rhi_count() == 0,
        "Texture Release must drop the cache-owned CPU and RHI latest versions");

    RenderResourceCache atomic_cache(make_placeholders());
    const MeshRenderResourceVersionRef atomic_mesh = make_mesh_version(300, 1);
    const TextureRenderResourceVersionRef linear_texture =
        make_placeholder_texture(301, TextureColorSemantic::Linear);
    TextureRenderResourceUpdate linear_update;
    linear_update.resource_id = linear_texture->resource_id;
    linear_update.version = linear_texture;
    atomic_cache.apply_updates(
        {mesh_update(atomic_mesh), RenderResourceUpdate(linear_update)});
    FakeDevice atomic_device;
    FakeGraphicsCommandContext atomic_context;
    auto atomic_result = atomic_cache.record_pending_uploads(
        atomic_device, atomic_context);
    check(atomic_result && atomic_result.value().mesh_count() == 1 &&
        atomic_result.value().texture_count() == 1 &&
        atomic_device.created_texture_descs.back().format ==
            RHIFormat::R8G8B8A8UNorm,
        "Mesh and linear Texture work must share one batch while Linear maps to UNorm");

    auto newer_linear_mutable = std::make_shared<TextureRenderResourceVersion>(
        *linear_texture);
    newer_linear_mutable->revision = RenderResourceRevision(2);
    TextureRenderResourceVersionRef newer_linear =
        std::move(newer_linear_mutable);
    linear_update.version = newer_linear;
    atomic_cache.apply_updates({RenderResourceUpdate(linear_update)});
    check(atomic_result &&
        !atomic_cache.commit_uploads(std::move(atomic_result).value()) &&
        atomic_cache.mesh_rhi_count() == 0 &&
        atomic_cache.texture_rhi_count() == 0,
        "One stale Texture must reject a mixed batch before any Mesh or Texture is published");

    auto invalid_semantic_texture = std::make_shared<TextureRenderResourceVersion>(
        *linear_texture);
    invalid_semantic_texture->resource_id = TextureRenderResourceId(302);
    invalid_semantic_texture->color_semantic =
        static_cast<TextureColorSemantic>(255);
    TextureRenderResourceUpdate invalid_semantic_update;
    invalid_semantic_update.resource_id = invalid_semantic_texture->resource_id;
    invalid_semantic_update.version = std::move(invalid_semantic_texture);
    check(atomic_cache.apply_updates(
            {RenderResourceUpdate(invalid_semantic_update)}).rejected_count == 1,
        "Texture Apply must reject an out-of-domain color semantic before RHI mapping");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " render resource upload check(s) failed.\n";
        return 1;
    }
    std::cout << "Render resource upload checks passed.\n";
    return 0;
}
