#pragma once

#include "rendercore/render_scene_update.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    enum class RenderSceneObjectType
    {
        Scene,
        Primitive,
        Light
    };

    enum class RenderSceneApplyError
    {
        InvalidScene,
        InvalidObjectId,
        DuplicateUpdateInBatch,
        DuplicateAdd,
        UnknownUpdate,
        UnknownRemove,
        InvalidDirtyFlags,
        InvalidSnapshot
    };

    struct RenderSceneApplyDiagnostic
    {
        RenderSceneApplyError error = RenderSceneApplyError::InvalidScene;
        RenderSceneObjectType object_type = RenderSceneObjectType::Scene;
        RenderSceneUpdateOperation operation = RenderSceneUpdateOperation::Update;
        std::uint64_t object_id = 0;
        std::string message;
    };

    struct RenderSceneApplyResult
    {
        std::size_t added_count = 0;
        std::size_t updated_count = 0;
        std::size_t removed_count = 0;
        std::size_t rejected_count = 0;
        std::vector<RenderSceneApplyDiagnostic> diagnostics;

        bool succeeded() const { return diagnostics.empty(); }
    };

    class PrimitiveSceneProxy
    {
    public:
        explicit PrimitiveSceneProxy(PrimitiveRenderSnapshot snapshot);

        const PrimitiveRenderSnapshot& snapshot() const { return snapshot_; }
        void replace_snapshot(PrimitiveRenderSnapshot snapshot);
        void update_transform(
            const Matrix4& world_transform,
            const AxisAlignedBounds& world_bounds);

    private:
        PrimitiveRenderSnapshot snapshot_;
    };

    class PrimitiveSceneInfo
    {
    public:
        PrimitiveSceneInfo(
            PrimitiveId primitive_id,
            PrimitiveRenderSnapshot snapshot);

        PrimitiveId primitive_id() const { return primitive_id_; }
        const PrimitiveSceneProxy& proxy() const { return proxy_; }

        void replace_proxy(PrimitiveRenderSnapshot snapshot);
        void update_transform(
            const Matrix4& world_transform,
            const AxisAlignedBounds& world_bounds);

    private:
        PrimitiveId primitive_id_;
        PrimitiveSceneProxy proxy_;
    };

    class LightSceneProxy
    {
    public:
        explicit LightSceneProxy(LightRenderSnapshot snapshot);

        const LightRenderSnapshot& snapshot() const { return snapshot_; }
        void replace_snapshot(LightRenderSnapshot snapshot);
        void update_transform(const Matrix4& world_transform);
        void update_dynamic_data(const LightRenderSnapshot& snapshot);

    private:
        LightRenderSnapshot snapshot_;
    };

    class LightSceneInfo
    {
    public:
        LightSceneInfo(LightId light_id, LightRenderSnapshot snapshot);

        LightId light_id() const { return light_id_; }
        const LightSceneProxy& proxy() const { return proxy_; }

        void replace_proxy(LightRenderSnapshot snapshot);
        void update_transform(const Matrix4& world_transform);
        void update_dynamic_data(const LightRenderSnapshot& snapshot);

    private:
        LightId light_id_;
        LightSceneProxy proxy_;
    };

    class RenderScene
    {
    public:
        explicit RenderScene(RenderSceneId scene_id);

        RenderSceneId scene_id() const { return scene_id_; }
        std::size_t primitive_count() const { return primitives_.size(); }
        std::size_t light_count() const { return lights_.size(); }

        const PrimitiveSceneInfo* find_primitive(PrimitiveId primitive_id) const;
        const LightSceneInfo* find_light(LightId light_id) const;
        RenderSceneApplyResult apply_updates(const RenderSceneUpdateBatch& batch);

    private:
        RenderSceneId scene_id_;
        std::unordered_map<std::uint64_t, PrimitiveSceneInfo> primitives_;
        std::unordered_map<std::uint64_t, LightSceneInfo> lights_;
    };
}
