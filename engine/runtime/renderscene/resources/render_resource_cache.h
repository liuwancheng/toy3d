#pragma once

#include "rendercore/render_resource_update.h"
#include "renderscene/resources/render_resource_upload.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    enum class RenderResourceType
    {
        Mesh,
        Material,
        Texture
    };

    enum class RenderResourceApplyError
    {
        InvalidUpdate,
        StaleRevision,
        RevisionConflict,
        UnknownRelease
    };

    struct RenderResourceApplyDiagnostic
    {
        RenderResourceApplyError error = RenderResourceApplyError::InvalidUpdate;
        RenderResourceType resource_type = RenderResourceType::Mesh;
        std::uint64_t resource_id = 0;
        RenderResourceRevision revision;
        std::string message;
    };

    struct RenderResourceApplyResult
    {
        std::size_t applied_count = 0;
        std::size_t released_count = 0;
        std::size_t unchanged_count = 0;
        std::size_t rejected_count = 0;
        std::vector<RenderResourceApplyDiagnostic> diagnostics;

        bool succeeded() const { return diagnostics.empty(); }
    };

    enum class RenderResourceResolveState
    {
        Found,
        Placeholder,
        Missing
    };

    template<typename VersionRef>
    struct RenderResourceResolveResult
    {
        RenderResourceResolveState state = RenderResourceResolveState::Missing;
        VersionRef version;

        explicit operator bool() const { return version != nullptr; }
    };

    struct RenderResourcePlaceholders
    {
        MaterialRenderResourceVersionRef error_material;
        TextureRenderResourceVersionRef checkerboard_texture;
        TextureRenderResourceVersionRef white_texture;
        TextureRenderResourceVersionRef normal_texture;
    };

    RenderResourcePlaceholders create_builtin_render_resource_placeholders();

    class RenderResourceCache final
    {
    public:
        explicit RenderResourceCache(RenderResourcePlaceholders placeholders);

        bool is_valid() const { return valid_; }
        bool rhi_placeholders_initialized() const
        {
            return rhi_placeholders_initialized_;
        }
        RHIStatus initialize_rhi_placeholders(RHIDevice& device);
        RenderResourceApplyResult apply_updates(
            const std::vector<RenderResourceUpdate>& updates);

        RenderResourceResolveResult<MeshRenderResourceVersionRef> resolve_mesh(
            MeshRenderResourceId resource_id) const;
        RenderResourceResolveResult<MaterialRenderResourceVersionRef> resolve_material(
            MaterialRenderResourceId resource_id) const;
        RenderResourceResolveResult<TextureRenderResourceVersionRef> resolve_texture(
            TextureRenderResourceId resource_id,
            TextureColorSemantic semantic) const;

        RHIResult<RenderResourceUploadBatch> record_pending_uploads(
            RHIDevice& device,
            RHIGraphicsCommandContext& context) const;
        RHIStatus commit_uploads(RenderResourceUploadBatch batch);
        RenderResourceResolveResult<MeshRHIResourceRef> resolve_mesh_rhi(
            MeshRenderResourceId resource_id) const;
        RenderResourceResolveResult<TextureRHIResourceRef> resolve_texture_rhi(
            TextureRenderResourceId resource_id) const;
        RenderResourceResolveResult<TextureRHIResourceRef> resolve_texture_rhi(
            TextureRenderResourceId resource_id,
            TextureColorSemantic semantic) const;

        std::size_t mesh_count() const { return meshes_.size(); }
        std::size_t material_count() const { return materials_.size(); }
        std::size_t texture_count() const { return textures_.size(); }
        std::size_t mesh_rhi_count() const { return mesh_rhi_resources_.size(); }
        std::size_t texture_rhi_count() const { return texture_rhi_resources_.size(); }

    private:
        void prune_stale_mesh_rhi_resource(MeshRenderResourceId resource_id);
        void prune_stale_texture_rhi_resource(TextureRenderResourceId resource_id);

        RenderResourcePlaceholders placeholders_;
        bool valid_ = false;
        bool rhi_placeholders_initialized_ = false;
        TextureRHIResourceRef checkerboard_texture_rhi_;
        TextureRHIResourceRef white_texture_rhi_;
        TextureRHIResourceRef normal_texture_rhi_;
        std::unordered_map<std::uint64_t, MeshRenderResourceVersionRef> meshes_;
        std::unordered_map<std::uint64_t, MaterialRenderResourceVersionRef> materials_;
        std::unordered_map<std::uint64_t, TextureRenderResourceVersionRef> textures_;
        std::unordered_map<std::uint64_t, MeshRHIResourceRef> mesh_rhi_resources_;
        std::unordered_map<std::uint64_t, TextureRHIResourceRef> texture_rhi_resources_;
    };
}
