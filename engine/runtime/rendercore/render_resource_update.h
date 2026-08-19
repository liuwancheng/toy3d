#pragma once

#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"
#include "rendercore/render_id.h"
#include "rendercore/render_resource_revision.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace toy3d
{
    struct MeshRenderResourceVersion
    {
        MeshRenderResourceId resource_id;
        RenderResourceRevision revision;
        std::vector<StaticMeshVertex> vertices;
        StaticMeshIndexData indices;
        std::vector<StaticMeshSection> sections;
    };

    struct MaterialRenderResourceVersion
    {
        MaterialRenderResourceId resource_id;
        RenderResourceRevision revision;
        MaterialDesc material;
    };

    enum class TextureColorSemantic
    {
        Color,
        Linear,
        Normal
    };

    struct TextureRenderResourceVersion
    {
        TextureRenderResourceId resource_id;
        RenderResourceRevision revision;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        TextureColorSemantic color_semantic = TextureColorSemantic::Color;
        std::vector<std::uint8_t> rgba8_pixels;
    };

    using MeshRenderResourceVersionRef =
        std::shared_ptr<const MeshRenderResourceVersion>;
    using MaterialRenderResourceVersionRef =
        std::shared_ptr<const MaterialRenderResourceVersion>;
    using TextureRenderResourceVersionRef =
        std::shared_ptr<const TextureRenderResourceVersion>;

    enum class RenderResourceUpdateOperation
    {
        Update,
        Release
    };

    template<typename Id, typename VersionRef>
    struct TypedRenderResourceUpdate
    {
        RenderResourceUpdateOperation operation = RenderResourceUpdateOperation::Update;
        Id resource_id;
        VersionRef version;
    };

    using MeshRenderResourceUpdate = TypedRenderResourceUpdate<
        MeshRenderResourceId,
        MeshRenderResourceVersionRef>;
    using MaterialRenderResourceUpdate = TypedRenderResourceUpdate<
        MaterialRenderResourceId,
        MaterialRenderResourceVersionRef>;
    using TextureRenderResourceUpdate = TypedRenderResourceUpdate<
        TextureRenderResourceId,
        TextureRenderResourceVersionRef>;

    // The resource domains have different IDs and payloads. A closed variant
    // preserves that type safety while allowing RenderFramePacket to keep the
    // confirmed single ordered resource-update stream.
    using RenderResourceUpdate = std::variant<
        MeshRenderResourceUpdate,
        MaterialRenderResourceUpdate,
        TextureRenderResourceUpdate>;
}
