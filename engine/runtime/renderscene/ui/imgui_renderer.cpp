#include "renderscene/ui/imgui_renderer.h"

#include <map>
#include <set>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_queue.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "shader_parameters/toy3d_ui_imgui.generated.h"

namespace toy3d
{
    const GlobalShaderType& imgui_global_shader_type()
    {
        static const ImGuiPassParameters parameters;
        const ShaderParametersMetadata& metadata = shader_parameters_metadata(parameters);
        static const GlobalShaderType type(
            "ImGuiGlobalShader", "Toy3d/UI/ImGui", "ImGui", shader::default_shader_permutation_key,
            GlobalShaderType::ProgramKind::Graphics, RHIShaderStageFlags::Vertex | RHIShaderStageFlags::Pixel, metadata,
            {GlobalShaderBindingRequirement(metadata.constant_buffer.binding_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::UniformBuffer, 1, RHIShaderStageFlags::Vertex),
             GlobalShaderBindingRequirement(metadata.resources[0u].parameter_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::SampledTexture, 1, RHIShaderStageFlags::Pixel),
             GlobalShaderBindingRequirement(metadata.resources[1u].parameter_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::Sampler, 1, RHIShaderStageFlags::Pixel)});
        return type;
    }

    namespace
    {
        const GlobalShaderTypeRegistration imgui_global_shader_registration(imgui_global_shader_type());

        Matrix4 make_projection(const ImGuiDrawData& draw_data)
        {
            const float left = draw_data.display_position[0];
            const float right = left + draw_data.display_size[0];
            const float top = draw_data.display_position[1];
            const float bottom = top + draw_data.display_size[1];
            return Matrix4(Vector4(2.0F / (right - left), 0.0F, 0.0F, 0.0F),
                           Vector4(0.0F, 2.0F / (top - bottom), 0.0F, 0.0F), Vector4(0.0F, 0.0F, 0.5F, 0.0F),
                           Vector4((right + left) / (left - right), (top + bottom) / (bottom - top), 0.5F, 1.0F));
        }

        bool make_scissor(const ImGuiDrawCommand& command, const ImGuiDrawData& data, const ImGuiPassTarget& target,
                          RHIRect& output)
        {
            const double scale_x = data.framebuffer_scale[0];
            const double scale_y = data.framebuffer_scale[1];
            const double left =
                std::max(0.0, (static_cast<double>(command.clip_rect.left) - data.display_position[0]) * scale_x);
            const double top =
                std::max(0.0, (static_cast<double>(command.clip_rect.top) - data.display_position[1]) * scale_y);
            const double right =
                std::min(static_cast<double>(target.extent.width),
                         (static_cast<double>(command.clip_rect.right) - data.display_position[0]) * scale_x);
            const double bottom =
                std::min(static_cast<double>(target.extent.height),
                         (static_cast<double>(command.clip_rect.bottom) - data.display_position[1]) * scale_y);
            if (!(right > left && bottom > top))
            {
                return false;
            }
            const double integer_left = std::floor(left);
            const double integer_top = std::floor(top);
            const double integer_right = std::ceil(right);
            const double integer_bottom = std::ceil(bottom);
            output.x = static_cast<std::int32_t>(integer_left);
            output.y = static_cast<std::int32_t>(integer_top);
            output.width = static_cast<std::uint32_t>(integer_right - integer_left);
            output.height = static_cast<std::uint32_t>(integer_bottom - integer_top);
            return output.width != 0u && output.height != 0u;
        }

        std::size_t grow_buffer_capacity(std::size_t required)
        {
            constexpr std::size_t minimum_capacity = 64u * 1024u;
            std::size_t capacity = minimum_capacity;
            while (capacity < required && capacity <= std::numeric_limits<std::size_t>::max() / 2u)
            {
                capacity *= 2u;
            }
            return capacity >= required ? capacity : required;
        }
    } // namespace

    RHIStatus ImGuiRenderer::initialize(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                        const GlobalShaderMap& global_shader_map, const ImGuiFontAtlasData& font_atlas)
    {
        if (initialized() || !font_atlas.valid())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGuiRenderer requires a valid one-time font atlas.");
        }
        RHITextureDesc font_desc;
        font_desc.width = font_atlas.width;
        font_desc.height = font_atlas.height;
        font_desc.format = PixelFormat::R8G8B8A8UNorm;
        font_desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
        font_desc.initial_access = RHIAccess::Common;
        font_desc.debug_name = "ImGuiFontAtlas";
        RHIResult<RHITextureRef> created_texture = device.create_texture(font_desc);
        if (!created_texture)
        {
            return created_texture.status();
        }

        RHITextureViewDesc view_desc;
        view_desc.type = RHIResourceViewType::ShaderResource;
        view_desc.format = font_desc.format;
        view_desc.debug_name = "ImGuiFontAtlasView";
        RHIResult<RHITextureViewRef> created_view = device.create_texture_view(created_texture.value(), view_desc);
        if (!created_view)
        {
            return created_view.status();
        }

        RHISamplerDesc sampler_desc;
        sampler_desc.address_u = RHIAddressMode::ClampToEdge;
        sampler_desc.address_v = RHIAddressMode::ClampToEdge;
        sampler_desc.address_w = RHIAddressMode::ClampToEdge;
        sampler_desc.debug_name = "ImGuiFontSampler";
        RHIResult<RHISamplerRef> created_sampler = device.create_sampler(sampler_desc);
        if (!created_sampler)
        {
            return created_sampler.status();
        }

        RHIStatus prepared = prepare_shader(device, shader_program_cache, global_shader_map);
        if (!prepared)
        {
            return prepared;
        }
        font_texture_ = std::move(created_texture).value();
        font_texture_view_ = std::move(created_view).value();
        font_sampler_ = std::move(created_sampler).value();
        publish_shader();
        return RHIStatus::success();
    }

    RHIStatus ImGuiRenderer::prepare_shader(RHIDevice& device, RHIShaderProgramCache& cache,
                                            const GlobalShaderMap& shaders)
    {
        discard_shader();
        const auto found = shaders.find(imgui_global_shader_type());
        if (!found.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, found.error);
        }
        const auto& data = found.program->data();
        auto created_program = cache.find_or_create(found.program);
        if (!created_program)
        {
            return created_program.status();
        }
        RHIGraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = created_program.value()->vertex_shader;
        pipeline_desc.pixel_shader = created_program.value()->pixel_shader;
        pipeline_desc.binding_layout = created_program.value()->binding_layout;
        pipeline_desc.primitive_topology = RHIPrimitiveTopology::TriangleList;
        pipeline_desc.color_attachment_count = 1u;
        pipeline_desc.color_formats[0] = PixelFormat::B8G8R8A8UNorm;
        pipeline_desc.sample_count = 1u;
        RHIGraphicsPipelineDesc::VertexBufferLayout vertex_buffer;
        vertex_buffer.binding = 0u;
        vertex_buffer.stride = sizeof(ImGuiVertex);
        pipeline_desc.vertex_buffers.push_back(vertex_buffer);
        for (const ShaderVertexInput& input : data.vertex_inputs)
        {
            RHIGraphicsPipelineDesc::VertexAttribute attribute;
            attribute.location = input.target_location;
            attribute.binding = 0u;
            switch (input.attribute_id)
            {
            case ShaderVertexAttributeId::Position0:
                attribute.format = PixelFormat::R32G32Float;
                attribute.offset = offsetof(ImGuiVertex, position);
                break;
            case ShaderVertexAttributeId::TexCoord0:
                attribute.format = PixelFormat::R32G32Float;
                attribute.offset = offsetof(ImGuiVertex, uv);
                break;
            case ShaderVertexAttributeId::Color0:
                attribute.format = PixelFormat::R8G8B8A8UNorm;
                attribute.offset = offsetof(ImGuiVertex, color);
                break;
            default:
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "ImGui vertex reflection contains an unsupported attribute.");
            }
            pipeline_desc.vertex_attributes.push_back(attribute);
        }
        if (pipeline_desc.vertex_attributes.size() != 3u)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGui vertex reflection must contain position, UV and color.");
        }
        pipeline_desc.debug_name = "ImGuiPipeline";
        const auto translated = build_shader_graphics_pipeline_desc(pipeline_desc, data.graphics_pass_state);
        if (!translated)
        {
            return translated.status();
        }
        const auto& depth = translated.value().depth_stencil;
        // UI has only a color attachment; incompatible state must fail before
        // publication instead of silently retaining the previous pipeline state.
        if (depth.depth_test_enable || depth.depth_write_enable || depth.stencil_test_enable)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGui Shader cannot enable depth or stencil without a depth attachment.");
        }
        RHIResult<RHIGraphicsPipelineRef> created_pipeline = device.create_graphics_pipeline(translated.value());
        if (!created_pipeline)
        {
            return created_pipeline.status();
        }

        pending_program_ = std::move(created_program).value();
        pending_pipeline_ = std::move(created_pipeline).value();
        return RHIStatus::success();
    }

    void ImGuiRenderer::publish_shader() noexcept
    {
        rhi_program_ = std::move(pending_program_);
        pipeline_ = std::move(pending_pipeline_);
    }

    void ImGuiRenderer::discard_shader() noexcept
    {
        pending_pipeline_.reset();
        pending_program_.reset();
    }

    RHIStatus ImGuiRenderer::record_font_upload(RHIGraphicsCommandContext& context,
                                                const ImGuiFontAtlasData& font_atlas)
    {
        if (!initialized() || bootstrap_complete_ || !font_atlas.valid())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGui font upload requires initialized unpublished resources.");
        }
        RHIResourceTransition to_copy;
        to_copy.resource = font_texture_;
        to_copy.before = RHIAccess::Common;
        to_copy.after = RHIAccess::CopyDestination;
        RHIStatus status = context.transition_resources({to_copy});
        if (!status)
        {
            return status;
        }
        RHITextureUploadDesc upload;
        upload.destination.texture = font_texture_;
        upload.extent = {font_atlas.width, font_atlas.height, 1u};
        upload.source.data = font_atlas.rgba_pixels.data();
        upload.source.size = font_atlas.rgba_pixels.size();
        upload.source.row_pitch = font_atlas.row_pitch;
        upload.source.slice_pitch = font_atlas.rgba_pixels.size();
        status = context.upload_texture(upload);
        if (!status)
        {
            return status;
        }
        RHIResourceTransition to_shader;
        to_shader.resource = font_texture_;
        to_shader.before = RHIAccess::CopyDestination;
        to_shader.after = RHIAccess::ShaderResourceGraphics;
        return context.transition_resources({to_shader});
    }

    void ImGuiRenderer::publish_bootstrap_complete() noexcept
    {
        if (initialized())
        {
            bootstrap_complete_ = true;
        }
    }

    void ImGuiRenderer::release() noexcept
    {
        discard_shader();
        recording_page_index_ = INVALID_PAGE_INDEX;
        buffer_pages_.clear();
        bootstrap_complete_ = false;
        pipeline_.reset();
        font_sampler_.reset();
        font_texture_view_.reset();
        font_texture_.reset();
        rhi_program_.reset();
    }

    RHIResult<std::size_t> ImGuiRenderer::acquire_buffer_page(RHIDevice& device, std::size_t vertex_bytes,
                                                              std::size_t index_bytes)
    {
        if (recording_page_index_ != INVALID_PAGE_INDEX)
        {
            return RHIResult<std::size_t>::failure(RHIErrorCode::InvalidArgument,
                                                   "ImGuiRenderer already has a buffer page in recording state.");
        }

        const RHIQueueCompletionValue completed_value = device.graphics_queue().completed_value();
        for (std::size_t index = 0u; index < buffer_pages_.size(); ++index)
        {
            BufferPage& page = buffer_pages_[index];
            const bool reusable =
                !page.recording && (page.completion_value == 0u || page.completion_value <= completed_value);
            if (reusable && page.vertex_capacity >= vertex_bytes && page.index_capacity >= index_bytes)
            {
                page.recording = true;
                page.completion_value = 0u;
                recording_page_index_ = index;
                return RHIResult<std::size_t>::success(index);
            }
        }

        BufferPage page;
        page.vertex_capacity = grow_buffer_capacity(vertex_bytes);
        page.index_capacity = grow_buffer_capacity(index_bytes);
        RHIBufferDesc vertex_desc;
        vertex_desc.size = page.vertex_capacity;
        vertex_desc.usage = RHIResourceUsage::VertexBuffer | RHIResourceUsage::CopyDestination;
        vertex_desc.initial_access = RHIAccess::Common;
        vertex_desc.debug_name = "ImGuiVertexBufferPage";
        RHIResult<RHIBufferRef> vertex_buffer = device.create_buffer(vertex_desc);
        if (!vertex_buffer)
        {
            return RHIResult<std::size_t>::failure(vertex_buffer.status().code(), vertex_buffer.status().message());
        }

        RHIBufferDesc index_desc;
        index_desc.size = page.index_capacity;
        index_desc.usage = RHIResourceUsage::IndexBuffer | RHIResourceUsage::CopyDestination;
        index_desc.initial_access = RHIAccess::Common;
        index_desc.debug_name = "ImGuiIndexBufferPage";
        RHIResult<RHIBufferRef> index_buffer = device.create_buffer(index_desc);
        if (!index_buffer)
        {
            return RHIResult<std::size_t>::failure(index_buffer.status().code(), index_buffer.status().message());
        }

        page.vertex_buffer = std::move(vertex_buffer).value();
        page.index_buffer = std::move(index_buffer).value();
        page.recording = true;
        buffer_pages_.push_back(std::move(page));
        recording_page_index_ = buffer_pages_.size() - 1u;
        return RHIResult<std::size_t>::success(recording_page_index_);
    }

    RHIStatus ImGuiRenderer::render(RHIDevice& device, RHIGraphicsCommandContext& context,
                                    const ImGuiDrawData& draw_data, const ImGuiPassTarget& target,
                                    const RHITextureViewRef& viewport_texture_view, ImGuiTextureId viewport_texture_id,
                                    const std::vector<ImGuiTextureBinding>& textures)
    {
        if (draw_data.empty())
        {
            return RHIStatus::success();
        }
        if (!ready())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGui rendering requires completed bootstrap resources.");
        }
        if (!target.color_view || target.extent.width == 0u || target.extent.height == 0u ||
            target.format != PixelFormat::B8G8R8A8UNorm || target.sample_count != 1u ||
            target.color_view->desc().format != target.format ||
            target.color_view->texture()->desc().sample_count != target.sample_count ||
            draw_data.framebuffer_width != target.extent.width ||
            draw_data.framebuffer_height != target.extent.height ||
            (draw_data.index_stride != 2u && draw_data.index_stride != 4u))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGui pass target or draw payload is incompatible.");
        }

        const std::size_t vertex_bytes = draw_data.vertices.size() * sizeof(ImGuiVertex);
        RHIResult<std::size_t> page_result = acquire_buffer_page(device, vertex_bytes, draw_data.indices.size());
        if (!page_result)
        {
            return page_result.status();
        }
        BufferPage& page = buffer_pages_[page_result.value()];

        RHIResourceTransition vertex_to_copy;
        vertex_to_copy.resource = page.vertex_buffer;
        vertex_to_copy.before = page.vertex_access;
        vertex_to_copy.after = RHIAccess::CopyDestination;
        RHIResourceTransition index_to_copy;
        index_to_copy.resource = page.index_buffer;
        index_to_copy.before = page.index_access;
        index_to_copy.after = RHIAccess::CopyDestination;
        RHIStatus status = context.transition_resources({vertex_to_copy, index_to_copy});
        if (!status)
        {
            return status;
        }
        RHIBufferUploadDesc vertex_upload;
        vertex_upload.destination = page.vertex_buffer;
        vertex_upload.source.data = draw_data.vertices.data();
        vertex_upload.source.size = vertex_bytes;
        status = context.upload_buffer(vertex_upload);
        if (!status)
        {
            return status;
        }
        RHIBufferUploadDesc index_upload;
        index_upload.destination = page.index_buffer;
        index_upload.source.data = draw_data.indices.data();
        index_upload.source.size = draw_data.indices.size();
        status = context.upload_buffer(index_upload);
        if (!status)
        {
            return status;
        }
        RHIResourceTransition vertex_ready;
        vertex_ready.resource = page.vertex_buffer;
        vertex_ready.before = RHIAccess::CopyDestination;
        vertex_ready.after = RHIAccess::VertexBuffer;
        RHIResourceTransition index_ready;
        index_ready.resource = page.index_buffer;
        index_ready.before = RHIAccess::CopyDestination;
        index_ready.after = RHIAccess::IndexBuffer;
        status = context.transition_resources({vertex_ready, index_ready});
        if (!status)
        {
            return status;
        }

        ImGuiPassParameters pass_parameters;
        pass_parameters.projection = make_projection(draw_data);
        pass_parameters.font_texture = font_texture_view_;
        pass_parameters.font_sampler = font_sampler_;
        RHIResult<RHIBindingSetRef> binding_set = create_transient_shader_binding(device, context, pass_parameters);
        if (!binding_set)
        {
            return binding_set.status();
        }
        RHIBindingSetRef font_binding = binding_set.value();
        std::vector<ImGuiTextureBinding> sources = textures;
        if (viewport_texture_view)
        {
            sources.push_back({viewport_texture_id, viewport_texture_view});
        }
        std::map<std::uint64_t, RHIBindingSetRef> bindings_by_id;
        std::set<std::uint64_t> declared_ids{IMGUI_FONT_ATLAS_TEXTURE_ID.value()};
        bindings_by_id.emplace(IMGUI_FONT_ATLAS_TEXTURE_ID.value(), font_binding);
        for (const ImGuiTextureBinding& source : sources)
        {
            if (!source.id.valid() || !source.view || !declared_ids.insert(source.id.value()).second)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "UI texture identity is invalid or duplicated.");
            }
            const bool referenced = std::any_of(draw_data.commands.begin(), draw_data.commands.end(),
                                                [&source](const ImGuiDrawCommand& command)
                                                {
                                                    return command.texture_id == source.id;
                                                });
            if (!referenced)
            {
                continue;
            }
            pass_parameters.font_texture = source.view;
            auto created = create_transient_shader_binding(device, context, pass_parameters);
            if (!created)
            {
                return created.status();
            }
            bindings_by_id.emplace(source.id.value(), created.value());
        }
        RHIBindingSetRef active_binding = font_binding;

        const auto bind_state = [&]() -> RHIStatus
        {
            RHIStatus bind_status = context.set_graphics_pipeline(pipeline_);
            if (!bind_status)
            {
                return bind_status;
            }
            RHIViewport viewport;
            viewport.width = static_cast<float>(target.extent.width);
            viewport.height = static_cast<float>(target.extent.height);
            bind_status = context.set_viewport(viewport);
            if (!bind_status)
            {
                return bind_status;
            }
            RHIVertexBufferBinding vertex_binding;
            vertex_binding.buffer = page.vertex_buffer;
            vertex_binding.stride = sizeof(ImGuiVertex);
            bind_status = context.set_vertex_buffers({vertex_binding});
            if (!bind_status)
            {
                return bind_status;
            }
            RHIIndexBufferBinding index_binding;
            index_binding.buffer = page.index_buffer;
            index_binding.format = draw_data.index_stride == 2u ? RHIIndexFormat::UInt16 : RHIIndexFormat::UInt32;
            bind_status = context.set_index_buffer(index_binding);
            if (!bind_status)
            {
                return bind_status;
            }
            RHIGraphicsBindings bindings;
            bindings.pass = active_binding;
            return context.bind_graphics_bindings(bindings);
        };

        RHIRenderPassDesc pass_desc;
        RHIColorAttachmentDesc attachment;
        attachment.view = target.color_view;
        attachment.load = target.load;
        attachment.store = RHIStoreOperation::Store;
        attachment.clear_value = target.clear_value;
        pass_desc.color_attachments.push_back(std::move(attachment));
        pass_desc.debug_name = "ImGuiPass";
        status = context.begin_render_pass(pass_desc);
        if (!status)
        {
            return status;
        }
        status = bind_state();
        if (!status)
        {
            return status;
        }
        for (const ImGuiDrawCommand& command : draw_data.commands)
        {
            if (command.reset_render_state)
            {
                status = bind_state();
                if (!status)
                {
                    return status;
                }
                continue;
            }
            const auto found = bindings_by_id.find(command.texture_id.value());
            if (found == bindings_by_id.end())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "ImGui draw references an unknown texture identity.");
            }
            RHIBindingSetRef requested_binding = found->second;
            if (requested_binding != active_binding)
            {
                active_binding = std::move(requested_binding);
                RHIGraphicsBindings bindings;
                bindings.pass = active_binding;
                status = context.bind_graphics_bindings(bindings);
                if (!status)
                {
                    return status;
                }
            }
            RHIRect scissor;
            if (!make_scissor(command, draw_data, target, scissor))
            {
                continue;
            }
            status = context.set_scissor(scissor);
            if (!status)
            {
                return status;
            }
            RHIDrawIndexedArgs draw;
            draw.index_count = command.element_count;
            draw.first_index = command.first_index;
            draw.vertex_offset = command.vertex_offset;
            status = context.draw_indexed(draw);
            if (!status)
            {
                return status;
            }
        }
        return context.end_render_pass();
    }

    RHIStatus ImGuiRenderer::publish_frame_submission(RHIQueueCompletionValue completion_value) noexcept
    {
        if (recording_page_index_ == INVALID_PAGE_INDEX || recording_page_index_ >= buffer_pages_.size() ||
            completion_value == 0u)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "ImGui buffer page publication requires a recording page and valid completion.");
        }
        BufferPage& page = buffer_pages_[recording_page_index_];
        page.vertex_access = RHIAccess::VertexBuffer;
        page.index_access = RHIAccess::IndexBuffer;
        page.completion_value = completion_value;
        page.recording = false;
        recording_page_index_ = INVALID_PAGE_INDEX;
        return RHIStatus::success();
    }

    void ImGuiRenderer::discard_frame_recording() noexcept
    {
        if (recording_page_index_ == INVALID_PAGE_INDEX || recording_page_index_ >= buffer_pages_.size())
        {
            return;
        }
        BufferPage& page = buffer_pages_[recording_page_index_];
        page.recording = false;
        page.completion_value = 0u;
        recording_page_index_ = INVALID_PAGE_INDEX;
    }

    bool ImGuiRenderer::initialized() const noexcept
    {
        return rhi_program_ && rhi_program_->vertex_shader && rhi_program_->pixel_shader &&
               rhi_program_->binding_layout && font_texture_ && font_texture_view_ && font_sampler_ && pipeline_;
    }

    bool ImGuiRenderer::ready() const noexcept
    {
        return initialized() && bootstrap_complete_;
    }
} // namespace toy3d
