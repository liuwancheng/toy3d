#include "renderscene/resources/render_resource_cache.h"

#include <cmath>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool vectors_equal(const vec2& left, const vec2& right)
        {
            return left.x == right.x && left.y == right.y;
        }

        bool vectors_equal(const vec3& left, const vec3& right)
        {
            return left.x == right.x && left.y == right.y && left.z == right.z;
        }

        bool is_finite(const vec2& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool is_finite(const vec3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        std::size_t index_count(const StaticMeshIndexData& indices)
        {
            // The fixed two-alternative variant models the actual index width;
            // explicit branches keep validation independent of visitor tricks.
            if (const auto* indices_u16 =
                std::get_if<std::vector<std::uint16_t>>(&indices))
            {
                return indices_u16->size();
            }
            if (const auto* indices_u32 =
                std::get_if<std::vector<std::uint32_t>>(&indices))
            {
                return indices_u32->size();
            }
            return 0;
        }

        bool indices_are_valid(
            const StaticMeshIndexData& indices,
            std::size_t vertex_count)
        {
            // get_if makes the two supported widths and their shared bounds
            // rule directly visible at this cross-thread validation boundary.
            if (const auto* indices_u16 =
                std::get_if<std::vector<std::uint16_t>>(&indices))
            {
                for (std::uint16_t index : *indices_u16)
                {
                    if (static_cast<std::size_t>(index) >= vertex_count)
                    {
                        return false;
                    }
                }
                return !indices_u16->empty();
            }
            if (const auto* indices_u32 =
                std::get_if<std::vector<std::uint32_t>>(&indices))
            {
                for (std::uint32_t index : *indices_u32)
                {
                    if (static_cast<std::size_t>(index) >= vertex_count)
                    {
                        return false;
                    }
                }
                return !indices_u32->empty();
            }
            return false;
        }

        bool mesh_version_is_valid(const MeshRenderResourceVersion& version)
        {
            if (!version.resource_id || !version.revision ||
                version.vertices.empty() || version.sections.empty() ||
                !indices_are_valid(version.indices, version.vertices.size()))
            {
                return false;
            }
            for (const StaticMeshVertex& vertex : version.vertices)
            {
                if (!is_finite(vertex.position) ||
                    !is_finite(vertex.normal) ||
                    !is_finite(vertex.uv0))
                {
                    return false;
                }
            }
            const std::size_t total_index_count = index_count(version.indices);
            for (const StaticMeshSection& section : version.sections)
            {
                const std::size_t first_index = section.first_index;
                const std::size_t section_index_count = section.index_count;
                if (section_index_count == 0 || section_index_count % 3 != 0 ||
                    first_index > total_index_count ||
                    section_index_count > total_index_count - first_index)
                {
                    return false;
                }
            }
            return true;
        }

        bool material_version_is_valid(const MaterialRenderResourceVersion& version)
        {
            return version.resource_id && version.revision &&
                !version.material.shader_name.empty();
        }

        bool texture_version_is_valid(const TextureRenderResourceVersion& version)
        {
            if (!version.resource_id || !version.revision ||
                version.width == 0 || version.height == 0)
            {
                return false;
            }
            switch (version.color_semantic)
            {
            case TextureColorSemantic::Color:
            case TextureColorSemantic::Linear:
            case TextureColorSemantic::Normal:
                break;
            default:
                return false;
            }
            constexpr std::size_t channel_count = 4;
            const std::size_t width = version.width;
            const std::size_t height = version.height;
            if (width > std::numeric_limits<std::size_t>::max() / height ||
                width * height >
                    std::numeric_limits<std::size_t>::max() / channel_count)
            {
                return false;
            }
            return version.rgba8_pixels.size() == width * height * channel_count;
        }

        bool index_data_equal(
            const StaticMeshIndexData& left,
            const StaticMeshIndexData& right)
        {
            // Equality must include the selected index width. Explicit get_if
            // branches avoid treating equal numeric indices as equal ABI data.
            if (const auto* left_u16 =
                std::get_if<std::vector<std::uint16_t>>(&left))
            {
                const auto* right_u16 =
                    std::get_if<std::vector<std::uint16_t>>(&right);
                return right_u16 != nullptr && *left_u16 == *right_u16;
            }
            if (const auto* left_u32 =
                std::get_if<std::vector<std::uint32_t>>(&left))
            {
                const auto* right_u32 =
                    std::get_if<std::vector<std::uint32_t>>(&right);
                return right_u32 != nullptr && *left_u32 == *right_u32;
            }
            return false;
        }

        bool mesh_content_equal(
            const MeshRenderResourceVersion& left,
            const MeshRenderResourceVersion& right)
        {
            if (left.vertices.size() != right.vertices.size() ||
                left.sections.size() != right.sections.size() ||
                !index_data_equal(left.indices, right.indices))
            {
                return false;
            }
            for (std::size_t index = 0; index < left.vertices.size(); ++index)
            {
                const StaticMeshVertex& left_vertex = left.vertices[index];
                const StaticMeshVertex& right_vertex = right.vertices[index];
                if (!vectors_equal(left_vertex.position, right_vertex.position) ||
                    !vectors_equal(left_vertex.normal, right_vertex.normal) ||
                    !vectors_equal(left_vertex.uv0, right_vertex.uv0))
                {
                    return false;
                }
            }
            for (std::size_t index = 0; index < left.sections.size(); ++index)
            {
                const StaticMeshSection& left_section = left.sections[index];
                const StaticMeshSection& right_section = right.sections[index];
                if (left_section.first_index != right_section.first_index ||
                    left_section.index_count != right_section.index_count ||
                    left_section.material_slot != right_section.material_slot)
                {
                    return false;
                }
            }
            return true;
        }

        bool material_content_equal(
            const MaterialRenderResourceVersion& left,
            const MaterialRenderResourceVersion& right)
        {
            return left.material.shader_name == right.material.shader_name &&
                left.material.shading_model == right.material.shading_model &&
                left.material.blend_mode == right.material.blend_mode &&
                left.material.two_sided == right.material.two_sided;
        }

        bool texture_content_equal(
            const TextureRenderResourceVersion& left,
            const TextureRenderResourceVersion& right)
        {
            return left.width == right.width &&
                left.height == right.height &&
                left.color_semantic == right.color_semantic &&
                left.rgba8_pixels == right.rgba8_pixels;
        }

        void reject_update(
            RenderResourceApplyResult& result,
            RenderResourceApplyError error,
            RenderResourceType resource_type,
            std::uint64_t resource_id,
            RenderResourceRevision revision,
            std::string message)
        {
            ++result.rejected_count;
            result.diagnostics.push_back(
                {error, resource_type, resource_id, revision, std::move(message)});
        }

        template<typename Update, typename VersionRef, typename Map>
        void apply_typed_update(
            const Update& update,
            RenderResourceType resource_type,
            bool (*validate)(const typename VersionRef::element_type&),
            bool (*content_equal)(
                const typename VersionRef::element_type&,
                const typename VersionRef::element_type&),
            Map& resources,
            RenderResourceApplyResult& result)
        {
            const std::uint64_t resource_id = update.resource_id.value();
            if (!update.resource_id)
            {
                reject_update(result, RenderResourceApplyError::InvalidUpdate,
                    resource_type, resource_id, {},
                    "A RenderResourceUpdate must identify a resource.");
                return;
            }

            const auto iterator = resources.find(resource_id);
            if (update.operation == RenderResourceUpdateOperation::Release)
            {
                if (update.version != nullptr)
                {
                    reject_update(result, RenderResourceApplyError::InvalidUpdate,
                        resource_type, resource_id, update.version->revision,
                        "A ReleaseResource update must not carry a version payload.");
                    return;
                }
                if (iterator == resources.end())
                {
                    reject_update(result, RenderResourceApplyError::UnknownRelease,
                        resource_type, resource_id, {},
                        "ReleaseResource targets an unknown latest entry.");
                    return;
                }
                resources.erase(iterator);
                ++result.released_count;
                return;
            }

            if (update.version == nullptr ||
                update.version->resource_id != update.resource_id ||
                !validate(*update.version))
            {
                reject_update(result, RenderResourceApplyError::InvalidUpdate,
                    resource_type, resource_id,
                    update.version != nullptr ? update.version->revision :
                        RenderResourceRevision{},
                    "A resource Update requires a valid immutable version with matching identity.");
                return;
            }
            if (iterator == resources.end())
            {
                resources.emplace(resource_id, update.version);
                ++result.applied_count;
                return;
            }

            const VersionRef& current = iterator->second;
            if (update.version->revision == current->revision)
            {
                if (content_equal(*update.version, *current))
                {
                    ++result.unchanged_count;
                    return;
                }
                reject_update(result, RenderResourceApplyError::RevisionConflict,
                    resource_type, resource_id, update.version->revision,
                    "The same resource revision cannot describe different content.");
                return;
            }
            if (update.version->revision < current->revision)
            {
                reject_update(result, RenderResourceApplyError::StaleRevision,
                    resource_type, resource_id, update.version->revision,
                    "A resource update must have a revision newer than the latest entry.");
                return;
            }
            iterator->second = update.version;
            ++result.applied_count;
        }
    }

    RenderResourceCache::RenderResourceCache(RenderResourcePlaceholders placeholders)
        : placeholders_(std::move(placeholders))
    {
        valid_ = placeholders_.error_material != nullptr &&
            material_version_is_valid(*placeholders_.error_material) &&
            placeholders_.checkerboard_texture != nullptr &&
            texture_version_is_valid(*placeholders_.checkerboard_texture) &&
            placeholders_.checkerboard_texture->color_semantic ==
                TextureColorSemantic::Color &&
            placeholders_.white_texture != nullptr &&
            texture_version_is_valid(*placeholders_.white_texture) &&
            placeholders_.white_texture->color_semantic ==
                TextureColorSemantic::Linear &&
            placeholders_.normal_texture != nullptr &&
            texture_version_is_valid(*placeholders_.normal_texture) &&
            placeholders_.normal_texture->color_semantic ==
                TextureColorSemantic::Normal;
    }

    RenderResourceApplyResult RenderResourceCache::apply_updates(
        const std::vector<RenderResourceUpdate>& updates)
    {
        RenderResourceApplyResult result;
        if (!valid_)
        {
            for (const RenderResourceUpdate& update : updates)
            {
                (void)update;
                reject_update(result, RenderResourceApplyError::InvalidUpdate,
                    RenderResourceType::Mesh, 0, {},
                    "RenderResourceCache requires valid immutable placeholders before Apply.");
            }
            return result;
        }

        for (const RenderResourceUpdate& update : updates)
        {
            // The update stream is a closed three-domain variant. Explicit
            // get_if branches preserve each strong ID and payload type through Apply.
            if (const auto* mesh_update =
                std::get_if<MeshRenderResourceUpdate>(&update))
            {
                apply_typed_update<
                    MeshRenderResourceUpdate,
                    MeshRenderResourceVersionRef>(
                    *mesh_update, RenderResourceType::Mesh,
                    mesh_version_is_valid, mesh_content_equal, meshes_, result);
                prune_stale_mesh_rhi_resource(mesh_update->resource_id);
                continue;
            }
            if (const auto* material_update =
                std::get_if<MaterialRenderResourceUpdate>(&update))
            {
                apply_typed_update<
                    MaterialRenderResourceUpdate,
                    MaterialRenderResourceVersionRef>(
                    *material_update, RenderResourceType::Material,
                    material_version_is_valid, material_content_equal,
                    materials_, result);
                continue;
            }
            if (const auto* texture_update =
                std::get_if<TextureRenderResourceUpdate>(&update))
            {
                apply_typed_update<
                    TextureRenderResourceUpdate,
                    TextureRenderResourceVersionRef>(
                    *texture_update, RenderResourceType::Texture,
                    texture_version_is_valid, texture_content_equal,
                    textures_, result);
                prune_stale_texture_rhi_resource(texture_update->resource_id);
            }
        }
        return result;
    }

    RenderResourceResolveResult<MeshRenderResourceVersionRef>
    RenderResourceCache::resolve_mesh(MeshRenderResourceId resource_id) const
    {
        const auto iterator = meshes_.find(resource_id.value());
        if (!resource_id || iterator == meshes_.end())
        {
            return {};
        }
        return {RenderResourceResolveState::Found, iterator->second};
    }

    RenderResourceResolveResult<MaterialRenderResourceVersionRef>
    RenderResourceCache::resolve_material(MaterialRenderResourceId resource_id) const
    {
        const auto iterator = materials_.find(resource_id.value());
        if (resource_id && iterator != materials_.end())
        {
            return {RenderResourceResolveState::Found, iterator->second};
        }
        return {RenderResourceResolveState::Placeholder, placeholders_.error_material};
    }

    RenderResourceResolveResult<TextureRenderResourceVersionRef>
    RenderResourceCache::resolve_texture(
        TextureRenderResourceId resource_id,
        TextureColorSemantic semantic) const
    {
        const auto iterator = textures_.find(resource_id.value());
        if (resource_id && iterator != textures_.end())
        {
            return {RenderResourceResolveState::Found, iterator->second};
        }
        switch (semantic)
        {
        case TextureColorSemantic::Color:
            return {RenderResourceResolveState::Placeholder,
                placeholders_.checkerboard_texture};
        case TextureColorSemantic::Linear:
            return {RenderResourceResolveState::Placeholder,
                placeholders_.white_texture};
        case TextureColorSemantic::Normal:
            return {RenderResourceResolveState::Placeholder,
                placeholders_.normal_texture};
        }
        return {};
    }
}
