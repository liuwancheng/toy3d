#include "renderscene/output/scene_output_resource_cache.h"

#include <set>
#include <string>
#include <utility>

namespace toy3d
{
    namespace
    {
        RHIStatus invalid_update(const std::string& message)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                message);
        }

        bool is_zero_extent(const SceneOutputExtent& extent)
        {
            return extent.width == 0 && extent.height == 0;
        }
    }

    RenderFrameExecutionStatus scene_output_apply_execution_status(
        const RHIStatus& status)
    {
        if (status)
        {
            return RenderFrameExecutionStatus::success();
        }
        switch (status.code())
        {
        case RHIErrorCode::InvalidArgument:
        case RHIErrorCode::Unsupported:
        case RHIErrorCode::OutOfMemory:
            return RenderFrameExecutionStatus::frame_failure(
                status.message(), status.code());
        case RHIErrorCode::NotReady:
        case RHIErrorCode::OutOfDate:
        case RHIErrorCode::Suboptimal:
        case RHIErrorCode::DeviceLost:
        case RHIErrorCode::BackendFailure:
            return RenderFrameExecutionStatus::fatal_failure(
                status.message(), status.code());
        case RHIErrorCode::None:
            break;
        }
        return RenderFrameExecutionStatus::fatal_failure(
            "SceneOutput apply received an invalid RHI error code.",
            status.code());
    }

    SceneOutputResourceCache::SceneOutputResourceCache(RHIDevice& device)
        : device_(device)
    {
    }

    SceneOutputApplyResult SceneOutputResourceCache::apply_updates(
        const std::vector<SceneOutputUpdate>& updates)
    {
        SceneOutputApplyResult result;
        std::set<std::uint64_t> batch_ids;
        for (const SceneOutputUpdate& update : updates)
        {
            if (!update.output_id || !update.revision)
            {
                result.status = invalid_update(
                    "SceneOutput updates require valid ID and revision values.");
                return result;
            }
            if (!batch_ids.emplace(update.output_id.value()).second)
            {
                result.status = invalid_update(
                    "A SceneOutput update batch cannot contain one ID more than once.");
                return result;
            }
            if ((update.extent.width == 0) != (update.extent.height == 0))
            {
                result.status = invalid_update(
                    "A SceneOutput update extent must have both dimensions zero or both non-zero.");
                return result;
            }

            const auto current = entries_.find(update.output_id.value());
            if (current != entries_.end())
            {
                if (current->second.released)
                {
                    result.status = invalid_update(
                        "A released SceneOutputId cannot be reused.");
                    return result;
                }
                if (update.revision <= current->second.latest_revision)
                {
                    result.status = invalid_update(
                        "A SceneOutput update revision must be newer than the current revision.");
                    return result;
                }
            }

            switch (update.operation)
            {
            case SceneOutputUpdateOperation::Update:
                if (current != entries_.end() &&
                    current->second.resource->extent == update.extent)
                {
                    result.status = invalid_update(
                        "A SceneOutput resize must change the current extent.");
                    return result;
                }
                break;
            case SceneOutputUpdateOperation::Release:
                if (current == entries_.end())
                {
                    result.status = invalid_update(
                        "A SceneOutput release requires a live output.");
                    return result;
                }
                if (!is_zero_extent(update.extent))
                {
                    result.status = invalid_update(
                        "A SceneOutput release must not carry an extent.");
                    return result;
                }
                break;
            default:
                result.status = invalid_update(
                    "A SceneOutput update has an invalid operation.");
                return result;
            }
        }

        struct StagedUpdate
        {
            const SceneOutputUpdate* update = nullptr;
            SceneOutputResourceRef resource;
        };
        std::vector<StagedUpdate> staged;
        staged.reserve(updates.size());
        for (const SceneOutputUpdate& update : updates)
        {
            StagedUpdate candidate;
            candidate.update = &update;
            if (update.operation == SceneOutputUpdateOperation::Update)
            {
                auto resource_result = create_resource(update);
                if (!resource_result)
                {
                    result.status = resource_result.status();
                    return result;
                }
                candidate.resource = std::move(resource_result).value();
            }
            staged.push_back(std::move(candidate));
        }

        // Build the complete next cache state away from the published map so a
        // node-allocation failure cannot expose only a prefix of this batch.
        auto next_entries = entries_;
        for (StagedUpdate& candidate : staged)
        {
            const SceneOutputUpdate& update = *candidate.update;
            Entry& entry = next_entries[update.output_id.value()];
            entry.latest_revision = update.revision;
            if (update.operation == SceneOutputUpdateOperation::Release)
            {
                entry.released = true;
                entry.resource.reset();
                ++result.released_count;
                continue;
            }
            entry.released = false;
            entry.resource = std::move(candidate.resource);
            ++result.updated_count;
        }
        entries_.swap(next_entries);
        return result;
    }

    SceneOutputResourceRef SceneOutputResourceCache::find(
        SceneOutputId output_id) const
    {
        const auto iterator = entries_.find(output_id.value());
        return output_id && iterator != entries_.end() &&
            !iterator->second.released
            ? iterator->second.resource
            : nullptr;
    }

    SceneOutputRevision SceneOutputResourceCache::latest_revision(
        SceneOutputId output_id) const
    {
        const auto iterator = entries_.find(output_id.value());
        return output_id && iterator != entries_.end()
            ? iterator->second.latest_revision
            : SceneOutputRevision{};
    }

    bool SceneOutputResourceCache::is_released(SceneOutputId output_id) const
    {
        const auto iterator = entries_.find(output_id.value());
        return output_id && iterator != entries_.end() &&
            iterator->second.released;
    }

    RHIResult<SceneOutputResourceRef>
        SceneOutputResourceCache::create_resource(
            const SceneOutputUpdate& update)
    {
        auto resource = std::make_shared<SceneOutputResource>();
        resource->output_id = update.output_id;
        resource->revision = update.revision;
        resource->extent = update.extent;
        if (is_zero_extent(update.extent))
        {
            return RHIResult<SceneOutputResourceRef>::success(
                std::move(resource));
        }

        RHITextureDesc texture_desc;
        texture_desc.width = update.extent.width;
        texture_desc.height = update.extent.height;
        texture_desc.format = RHIFormat::R8G8B8A8UNorm;
        texture_desc.usage = rhi_enum_or(
            RHIResourceUsage::RenderTarget,
            RHIResourceUsage::ShaderResource);
        texture_desc.initial_access = RHIAccess::Common;
        texture_desc.clear_value = RHIClearValue::Black;
        texture_desc.debug_name = "SceneOutput." +
            std::to_string(update.output_id.value());

        RHIStatus status = validate_texture_desc(texture_desc);
        if (!status)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                status.code(), status.message());
        }
        status = validate_texture_format_capabilities(
            texture_desc,
            device_.format_capabilities(texture_desc.format));
        if (!status)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                status.code(), status.message());
        }

        auto texture_result = device_.create_texture(texture_desc);
        if (!texture_result)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                texture_result.status().code(),
                texture_result.status().message());
        }
        resource->texture = std::move(texture_result).value();

        RHITextureViewDesc render_target_desc;
        render_target_desc.type = RHIResourceViewType::RenderTarget;
        render_target_desc.format = texture_desc.format;
        render_target_desc.debug_name = texture_desc.debug_name + ".RTV";
        status = validate_texture_view_desc(
            texture_desc, render_target_desc);
        if (!status)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                status.code(), status.message());
        }
        auto render_target_result = device_.create_texture_view(
            resource->texture, render_target_desc);
        if (!render_target_result)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                render_target_result.status().code(),
                render_target_result.status().message());
        }
        resource->render_target_view =
            std::move(render_target_result).value();

        RHITextureViewDesc shader_resource_desc;
        shader_resource_desc.type = RHIResourceViewType::ShaderResource;
        shader_resource_desc.format = texture_desc.format;
        shader_resource_desc.debug_name = texture_desc.debug_name + ".SRV";
        status = validate_texture_view_desc(
            texture_desc, shader_resource_desc);
        if (!status)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                status.code(), status.message());
        }
        auto shader_resource_result = device_.create_texture_view(
            resource->texture, shader_resource_desc);
        if (!shader_resource_result)
        {
            return RHIResult<SceneOutputResourceRef>::failure(
                shader_resource_result.status().code(),
                shader_resource_result.status().message());
        }
        resource->shader_resource_view =
            std::move(shader_resource_result).value();
        return RHIResult<SceneOutputResourceRef>::success(
            std::move(resource));
    }
}
