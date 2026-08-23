#include "drivers/rhi/rhi_pipeline_cache.h"

#include <condition_variable>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace toy3d
{
    namespace
    {
        template<typename T>
        void hash_combine(std::size_t& seed, const T& value)
        {
            seed ^= std::hash<T>{}(value) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        }

        template<typename T>
        void hash_enum(std::size_t& seed, T value)
        {
            using Underlying = typename std::underlying_type<T>::type;
            hash_combine(seed, static_cast<Underlying>(value));
        }

        bool equal_vertex_buffer_layout(
            const RHIGraphicsPipelineDesc::VertexBufferLayout& left,
            const RHIGraphicsPipelineDesc::VertexBufferLayout& right)
        {
            return left.binding == right.binding &&
                left.stride == right.stride &&
                left.input_rate == right.input_rate;
        }

        bool equal_vertex_attribute(
            const RHIGraphicsPipelineDesc::VertexAttribute& left,
            const RHIGraphicsPipelineDesc::VertexAttribute& right)
        {
            return left.location == right.location &&
                left.binding == right.binding &&
                left.format == right.format &&
                left.offset == right.offset;
        }

        bool equal_rasterization_state(
            const RHIGraphicsPipelineDesc::RasterizationState& left,
            const RHIGraphicsPipelineDesc::RasterizationState& right)
        {
            return left.polygon_mode == right.polygon_mode &&
                left.cull_mode == right.cull_mode &&
                left.front_face == right.front_face &&
                left.depth_clamp_enable == right.depth_clamp_enable;
        }

        bool equal_stencil_face_state(
            const RHIGraphicsPipelineDesc::StencilFaceState& left,
            const RHIGraphicsPipelineDesc::StencilFaceState& right)
        {
            return left.fail_operation == right.fail_operation &&
                left.depth_fail_operation == right.depth_fail_operation &&
                left.pass_operation == right.pass_operation &&
                left.compare_operation == right.compare_operation;
        }

        bool equal_depth_stencil_state(
            const RHIGraphicsPipelineDesc::DepthStencilState& left,
            const RHIGraphicsPipelineDesc::DepthStencilState& right)
        {
            return left.depth_test_enable == right.depth_test_enable &&
                left.depth_write_enable == right.depth_write_enable &&
                left.depth_compare_operation == right.depth_compare_operation &&
                left.stencil_test_enable == right.stencil_test_enable &&
                left.stencil_read_mask == right.stencil_read_mask &&
                left.stencil_write_mask == right.stencil_write_mask &&
                equal_stencil_face_state(left.front_face, right.front_face) &&
                equal_stencil_face_state(left.back_face, right.back_face);
        }

        bool equal_color_blend_attachment(
            const RHIGraphicsPipelineDesc::ColorBlendAttachmentState& left,
            const RHIGraphicsPipelineDesc::ColorBlendAttachmentState& right)
        {
            return left.blend_enable == right.blend_enable &&
                left.source_color_factor == right.source_color_factor &&
                left.destination_color_factor == right.destination_color_factor &&
                left.color_operation == right.color_operation &&
                left.source_alpha_factor == right.source_alpha_factor &&
                left.destination_alpha_factor == right.destination_alpha_factor &&
                left.alpha_operation == right.alpha_operation &&
                left.color_write_mask == right.color_write_mask;
        }

        template<typename T, typename EqualFunction>
        bool equal_vectors(
            const std::vector<T>& left,
            const std::vector<T>& right,
            EqualFunction equal_function)
        {
            if (left.size() != right.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (!equal_function(left[index], right[index]))
                {
                    return false;
                }
            }
            return true;
        }

        RHIShaderKey make_shader_key(const RHIShaderRef& shader)
        {
            const RHIShaderDesc& desc = shader->desc();
            RHIShaderKey key;
            key.stage = desc.stage;
            key.target = desc.bytecode.target;
            key.entry_point = desc.entry_point;
            key.content_hash = desc.content_hash;
            key.bytecode = desc.bytecode.bytes;
            key.vertex_inputs = desc.vertex_inputs;
            return key;
        }

        void hash_shader_key(std::size_t& seed, const RHIShaderKey& key)
        {
            hash_enum(seed, key.stage);
            hash_combine(seed, key.target);
            hash_combine(seed, key.entry_point);
            hash_combine(seed, key.content_hash[0]);
            hash_combine(seed, key.content_hash[1]);
            hash_combine(seed, key.vertex_inputs.size());
            for (const RHIShaderVertexInputReflection& input : key.vertex_inputs)
            {
                hash_combine(seed, input.semantic_name);
                hash_combine(seed, input.semantic_index);
                hash_combine(seed, input.location);
                hash_enum(seed, input.scalar_type);
                hash_combine(seed, input.component_count);
            }
        }

        void hash_stencil_face(
            std::size_t& seed,
            const RHIGraphicsPipelineDesc::StencilFaceState& state)
        {
            hash_enum(seed, state.fail_operation);
            hash_enum(seed, state.depth_fail_operation);
            hash_enum(seed, state.pass_operation);
            hash_enum(seed, state.compare_operation);
        }

        void hash_blend_attachment(
            std::size_t& seed,
            const RHIGraphicsPipelineDesc::ColorBlendAttachmentState& state)
        {
            hash_combine(seed, state.blend_enable);
            hash_enum(seed, state.source_color_factor);
            hash_enum(seed, state.destination_color_factor);
            hash_enum(seed, state.color_operation);
            hash_enum(seed, state.source_alpha_factor);
            hash_enum(seed, state.destination_alpha_factor);
            hash_enum(seed, state.alpha_operation);
            hash_enum(seed, state.color_write_mask);
        }
    }

    bool RHIShaderKey::operator==(const RHIShaderKey& other) const
    {
        return stage == other.stage &&
            target == other.target &&
            entry_point == other.entry_point &&
            content_hash == other.content_hash &&
            bytecode == other.bytecode &&
            vertex_inputs == other.vertex_inputs;
    }

    bool RHIGraphicsPipelineKey::operator==(const RHIGraphicsPipelineKey& other) const
    {
        if (!(vertex_shader == other.vertex_shader) ||
            !(pixel_shader == other.pixel_shader) ||
            !(binding_layout == other.binding_layout) ||
            primitive_topology != other.primitive_topology ||
            !equal_vectors(vertex_buffers, other.vertex_buffers, equal_vertex_buffer_layout) ||
            !equal_vectors(vertex_attributes, other.vertex_attributes, equal_vertex_attribute) ||
            !equal_rasterization_state(rasterization, other.rasterization) ||
            !equal_depth_stencil_state(depth_stencil, other.depth_stencil) ||
            color_attachment_count != other.color_attachment_count ||
            depth_stencil_format != other.depth_stencil_format ||
            sample_count != other.sample_count)
        {
            return false;
        }
        for (std::uint32_t index = 0; index < color_attachment_count; ++index)
        {
            if (color_formats[index] != other.color_formats[index] ||
                !equal_color_blend_attachment(
                    color_blend_attachments[index],
                    other.color_blend_attachments[index]))
            {
                return false;
            }
        }
        return true;
    }

    RHIGraphicsPipelineDesc canonicalize_graphics_pipeline_desc(
        const RHIGraphicsPipelineDesc& desc)
    {
        RHIGraphicsPipelineDesc result = desc;
        for (std::uint32_t index = 0; index < RHI_MAX_COLOR_ATTACHMENTS; ++index)
        {
            if (index >= result.color_attachment_count)
            {
                result.color_formats[index] = PixelFormat::Unknown;
                result.color_blend_attachments[index] = {};
                continue;
            }
            auto& blend = result.color_blend_attachments[index];
            if (!blend.blend_enable)
            {
                const RHIColorWriteMask write_mask = blend.color_write_mask;
                blend = {};
                blend.color_write_mask = write_mask;
            }
        }
        if (!result.depth_stencil.depth_test_enable)
        {
            result.depth_stencil.depth_compare_operation = RHICompareOperation::LessEqual;
        }
        if (!result.depth_stencil.stencil_test_enable)
        {
            result.depth_stencil.stencil_read_mask = 0xffU;
            result.depth_stencil.stencil_write_mask = 0xffU;
            result.depth_stencil.front_face = {};
            result.depth_stencil.back_face = {};
        }
        return result;
    }

    RHIGraphicsPipelineKey make_graphics_pipeline_key(
        const RHIGraphicsPipelineDesc& canonical_desc)
    {
        RHIGraphicsPipelineKey key;
        key.vertex_shader = make_shader_key(canonical_desc.vertex_shader);
        key.pixel_shader = make_shader_key(canonical_desc.pixel_shader);
        key.binding_layout = canonical_desc.binding_layout->desc();
        key.binding_layout.debug_name.clear();
        key.primitive_topology = canonical_desc.primitive_topology;
        key.vertex_buffers = canonical_desc.vertex_buffers;
        key.vertex_attributes = canonical_desc.vertex_attributes;
        key.rasterization = canonical_desc.rasterization;
        key.depth_stencil = canonical_desc.depth_stencil;
        key.color_formats = canonical_desc.color_formats;
        key.color_blend_attachments = canonical_desc.color_blend_attachments;
        key.color_attachment_count = canonical_desc.color_attachment_count;
        key.depth_stencil_format = canonical_desc.depth_stencil_format;
        key.sample_count = canonical_desc.sample_count;
        return key;
    }

    std::size_t hash_graphics_pipeline_key(const RHIGraphicsPipelineKey& key)
    {
        std::size_t seed = 0;
        hash_shader_key(seed, key.vertex_shader);
        hash_shader_key(seed, key.pixel_shader);
        for (const RHIBindingLayoutEntry& entry : key.binding_layout.entries)
        {
            hash_enum(seed, entry.group);
            hash_combine(seed, entry.slot);
            hash_enum(seed, entry.type);
            hash_enum(seed, entry.stages);
            hash_combine(seed, entry.array_count);
        }
        hash_enum(seed, key.primitive_topology);
        for (const auto& layout : key.vertex_buffers)
        {
            hash_combine(seed, layout.binding);
            hash_combine(seed, layout.stride);
            hash_enum(seed, layout.input_rate);
        }
        for (const auto& attribute : key.vertex_attributes)
        {
            hash_combine(seed, attribute.location);
            hash_combine(seed, attribute.binding);
            hash_enum(seed, attribute.format);
            hash_combine(seed, attribute.offset);
        }
        hash_enum(seed, key.rasterization.polygon_mode);
        hash_enum(seed, key.rasterization.cull_mode);
        hash_enum(seed, key.rasterization.front_face);
        hash_combine(seed, key.rasterization.depth_clamp_enable);
        hash_combine(seed, key.depth_stencil.depth_test_enable);
        hash_combine(seed, key.depth_stencil.depth_write_enable);
        hash_enum(seed, key.depth_stencil.depth_compare_operation);
        hash_combine(seed, key.depth_stencil.stencil_test_enable);
        hash_combine(seed, key.depth_stencil.stencil_read_mask);
        hash_combine(seed, key.depth_stencil.stencil_write_mask);
        hash_stencil_face(seed, key.depth_stencil.front_face);
        hash_stencil_face(seed, key.depth_stencil.back_face);
        hash_combine(seed, key.color_attachment_count);
        for (std::uint32_t index = 0; index < key.color_attachment_count; ++index)
        {
            hash_enum(seed, key.color_formats[index]);
            hash_blend_attachment(seed, key.color_blend_attachments[index]);
        }
        hash_enum(seed, key.depth_stencil_format);
        hash_combine(seed, key.sample_count);
        return seed;
    }

    class RHIGraphicsPipelineCache::Impl final
    {
    public:
        struct Entry
        {
            explicit Entry(RHIGraphicsPipelineKey pipeline_key)
                : key(std::move(pipeline_key))
            {
            }

            RHIGraphicsPipelineKey key;
            RHIGraphicsPipelineRef pipeline;
            RHIStatus creation_status;
            bool creating = true;
            std::condition_variable ready;
        };

        std::mutex mutex;
        std::unordered_map<std::size_t, std::vector<std::shared_ptr<Entry>>> entries;
    };

    RHIGraphicsPipelineCache::RHIGraphicsPipelineCache()
        : implementation(std::make_unique<Impl>())
    {
    }

    RHIGraphicsPipelineCache::~RHIGraphicsPipelineCache() = default;

    RHIResult<RHIGraphicsPipelineRef> RHIGraphicsPipelineCache::get_or_create(
        const RHIGraphicsPipelineDesc& desc,
        const CreateFunction& create_function)
    {
        RHIGraphicsPipelineDesc canonical_desc = canonicalize_graphics_pipeline_desc(desc);
        RHIGraphicsPipelineKey key = make_graphics_pipeline_key(canonical_desc);
        const std::size_t hash = hash_graphics_pipeline_key(key);
        std::shared_ptr<Impl::Entry> entry;
        bool create_pipeline = false;
        {
            std::unique_lock<std::mutex> lock(implementation->mutex);
            auto& bucket = implementation->entries[hash];
            for (const std::shared_ptr<Impl::Entry>& candidate : bucket)
            {
                if (candidate->key == key)
                {
                    entry = candidate;
                    break;
                }
            }
            if (!entry)
            {
                entry = std::make_shared<Impl::Entry>(std::move(key));
                bucket.push_back(entry);
                create_pipeline = true;
            }
            else
            {
                while (entry->creating)
                {
                    entry->ready.wait(lock);
                }
                if (entry->pipeline)
                {
                    return RHIResult<RHIGraphicsPipelineRef>::success(entry->pipeline);
                }
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    entry->creation_status.code(),
                    entry->creation_status.message());
            }
        }

        if (!create_pipeline)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::BackendFailure,
                "Graphics pipeline cache entered an invalid creation state.");
        }

        RHIResult<RHIGraphicsPipelineRef> result = create_function(canonical_desc);
        if (result && !result.value())
        {
            result = RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::BackendFailure,
                "Backend returned a null graphics pipeline after successful creation.");
        }
        {
            std::lock_guard<std::mutex> lock(implementation->mutex);
            if (result)
            {
                entry->pipeline = result.value();
            }
            else
            {
                entry->creation_status = result.status();
                auto bucket_iterator = implementation->entries.find(hash);
                if (bucket_iterator != implementation->entries.end())
                {
                    auto& bucket = bucket_iterator->second;
                    for (auto iterator = bucket.begin(); iterator != bucket.end(); ++iterator)
                    {
                        if (*iterator == entry)
                        {
                            bucket.erase(iterator);
                            break;
                        }
                    }
                    if (bucket.empty())
                    {
                        implementation->entries.erase(bucket_iterator);
                    }
                }
            }
            entry->creating = false;
        }
        entry->ready.notify_all();
        return result;
    }

    void RHIGraphicsPipelineCache::clear()
    {
        decltype(implementation->entries) retired_entries;
        {
            std::lock_guard<std::mutex> lock(implementation->mutex);
            retired_entries.swap(implementation->entries);
        }
    }
}
