#include "drivers/rhi/rhi_queue.h"
#include "drivers/vulkan/vulkan_command_context.h"
#include "drivers/vulkan/vulkan_deferred_deletion.h"
#include "drivers/vulkan/vulkan_device.h"
#include "drivers/vulkan/vulkan_memory_manager.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

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

    class ConcurrentSubmitCommandList final : public toy3d::RHICommandList
    {
      public:
        bool close_for_submit() { return mark_recording() && mark_closed(); }
    };

    class ConcurrentSubmitQueue final : public toy3d::RHIQueue
    {
      public:
        toy3d::RHIQueueCompletionValue completed_value() const override { return submit_count.load(); }

        toy3d::RHIStatus wait_for_value(toy3d::RHIQueueCompletionValue) override { return toy3d::RHIStatus::success(); }

        toy3d::RHIStatus wait_idle() override { return toy3d::RHIStatus::success(); }

        std::atomic<toy3d::RHIQueueCompletionValue> submit_count{0};

        void complete() { retained_lists.clear(); }

      protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(const toy3d::RHISubmitInfo& info) override
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            retained_lists = info.command_lists;
            return toy3d::RHIResult<toy3d::RHISubmitResult>::success({++submit_count});
        }

      private:
        std::vector<toy3d::RHICommandListRef> retained_lists;
    };
} // namespace

int main()
{
    toy3d::VulkanDevice first_device;
    toy3d::VulkanDevice second_device;

    toy3d::RHIBufferDesc buffer_desc;
    buffer_desc.size = 16;
    auto buffer = std::make_shared<toy3d::RHIBuffer>(first_device, buffer_desc);
    check(buffer->owner_device() == &first_device, "RHI objects must retain their immutable creating-device identity");
    check(buffer->is_owned_by(first_device) && !buffer->is_owned_by(second_device),
          "RHI object ownership checks must distinguish devices of the same backend");

    toy3d::RHITextureDesc texture_desc;
    texture_desc.width = 1;
    texture_desc.height = 1;
    texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
    auto foreign_texture = std::make_shared<toy3d::RHITexture>(second_device, texture_desc);

    toy3d::RHITextureViewDesc view_desc;
    view_desc.format = texture_desc.format;
    const auto rejected_view = first_device.create_texture_view(foreign_texture, view_desc);
    check(!rejected_view && rejected_view.status().code() == toy3d::RHIErrorCode::InvalidArgument,
          "Vulkan view creation must reject a same-backend resource from another device before native work");

    auto inherited_view = std::make_shared<toy3d::RHITextureView>(foreign_texture, view_desc);
    check(inherited_view->is_owned_by(second_device), "RHI views must inherit the viewed resource's device identity");

    auto command_pool = std::make_shared<toy3d::VulkanCommandPool>(VK_NULL_HANDLE, VK_NULL_HANDLE);
    toy3d::RHITextureDesc tracked_texture_desc;
    tracked_texture_desc.width = 1;
    tracked_texture_desc.height = 1;
    tracked_texture_desc.mip_levels = 1;
    tracked_texture_desc.array_layers = 1;
    auto tracked_texture = std::make_shared<toy3d::VulkanTexture>(first_device, tracked_texture_desc, VK_NULL_HANDLE,
                                                                  VK_IMAGE_LAYOUT_GENERAL, toy3d::RHIAccess::Common);
    const toy3d::RHISubresourceRange full_texture_range;
    {
        toy3d::VulkanCommandList discarded_list(first_device, command_pool, VK_NULL_HANDLE, "Discarded");
        discarded_list.track_texture_transition(tracked_texture, full_texture_range,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                toy3d::RHIAccess::CopyDestination);
        check(tracked_texture->current_access() == toy3d::RHIAccess::Common,
              "discarded local final state must not modify committed texture state");
    }
    check(tracked_texture->current_access() == toy3d::RHIAccess::Common,
          "destroying an unsubmitted list must leave committed texture state unchanged");

    toy3d::VulkanCommandList first_recorded_list(first_device, command_pool, VK_NULL_HANDLE, "FirstRecorded");
    first_recorded_list.track_texture_transition(tracked_texture, full_texture_range,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                 toy3d::RHIAccess::ShaderResourceGraphics);
    toy3d::VulkanCommandList first_submitted_list(first_device, command_pool, VK_NULL_HANDLE, "FirstSubmitted");
    first_submitted_list.track_texture_transition(
        tracked_texture, full_texture_range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, toy3d::RHIAccess::CopyDestination);
    check(static_cast<bool>(first_submitted_list.validate_committed_resource_states()),
          "the first submitted list must validate against current committed state");
    first_submitted_list.commit_resource_states();
    check(!first_recorded_list.validate_committed_resource_states(),
          "a later submit must reject a list whose first-required state became stale");
    check(tracked_texture->current_access() == toy3d::RHIAccess::CopyDestination,
          "committed state must follow submit order rather than recording order");

    toy3d::VulkanMemoryManager memory_manager;
    toy3d::VulkanDeferredDeletionQueue deletion_queue;
    toy3d::RHIBufferDesc tracked_buffer_desc;
    tracked_buffer_desc.size = 16;
    auto tracked_buffer =
        std::make_shared<toy3d::VulkanBuffer>(first_device, tracked_buffer_desc, memory_manager, deletion_queue,
                                              toy3d::VulkanAllocatedBuffer{}, toy3d::RHIAccess::Common);
    {
        toy3d::VulkanCommandList upload_draw_list(first_device, command_pool, VK_NULL_HANDLE, "UploadDraw");
        upload_draw_list.track_buffer_transition(tracked_buffer, toy3d::RHIAccess::CopyDestination);
        check(upload_draw_list.tracked_buffer_access(tracked_buffer) == toy3d::RHIAccess::CopyDestination,
              "same-list upload must observe the local CopyDestination state");
        upload_draw_list.track_buffer_transition(tracked_buffer, toy3d::RHIAccess::VertexBuffer);
        check(upload_draw_list.tracked_buffer_access(tracked_buffer) == toy3d::RHIAccess::VertexBuffer,
              "same-list draw must observe the post-upload local VertexBuffer state");
        check(tracked_buffer->current_access() == toy3d::RHIAccess::Common,
              "same-list transitions must remain local before submit");
        upload_draw_list.commit_resource_states();
    }
    check(tracked_buffer->current_access() == toy3d::RHIAccess::VertexBuffer,
          "successful submit publication must expose the list final access");

    ConcurrentSubmitQueue submit_queue;
    auto command_list = std::make_shared<ConcurrentSubmitCommandList>();
    check(command_list->close_for_submit(), "resource-state test command list must become closed");
    toy3d::RHISubmitInfo submit_info;
    submit_info.command_lists.push_back(command_list);
    std::atomic<bool> start{false};
    std::atomic<int> success_count{0};
    std::atomic<int> rejected_count{0};
    const auto submit_once = [&]()
    {
        while (!start.load())
        {
            std::this_thread::yield();
        }
        const auto result = submit_queue.submit(submit_info);
        if (result)
        {
            ++success_count;
        }
        else if (result.status().code() == toy3d::RHIErrorCode::InvalidArgument)
        {
            ++rejected_count;
        }
    };
    std::thread first_submit(submit_once);
    std::thread second_submit(submit_once);
    start.store(true);
    first_submit.join();
    second_submit.join();
    check(success_count.load() == 1 && rejected_count.load() == 1,
          "concurrent duplicate command-list submit must publish exactly one success");
    check(submit_queue.submit_count.load() == 1, "concurrent duplicate submit must enter the backend exactly once");
    check(command_list->state() == toy3d::RHICommandListState::Submitted,
          "backend success must publish Submitted without a post-submit failure path");

    ConcurrentSubmitQueue completion_queue;
    auto payload_list =
        std::make_shared<toy3d::VulkanCommandList>(first_device, command_pool, VK_NULL_HANDLE, "CompletionPayload");
    check(payload_list->begin_recording_by_context() && payload_list->close_by_context(),
          "payload command list must become closed");
    payload_list->retain_resource(tracked_buffer);
    std::weak_ptr<toy3d::VulkanBuffer> payload_observer = tracked_buffer;
    toy3d::RHISubmitInfo payload_submit_info;
    payload_submit_info.command_lists.push_back(payload_list);
    check(static_cast<bool>(completion_queue.submit(payload_submit_info)), "payload command list submit must succeed");
    payload_submit_info.command_lists.clear();
    payload_list.reset();
    tracked_buffer.reset();
    check(!payload_observer.expired(), "submitted payload must remain alive before queue completion");
    completion_queue.complete();
    check(payload_observer.expired(),
          "submitted payload may retire only after queue completion releases its command list");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI resource-state tests passed\n";
    return 0;
}
