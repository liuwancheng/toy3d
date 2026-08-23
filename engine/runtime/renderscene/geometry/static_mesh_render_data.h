#pragma once

namespace toy3d
{
    // Render-side StaticMesh representation. Batch B exposes only the complete
    // drawable gate; task 10.4 will own and validate the complete resource candidate.
    class StaticMeshRenderData final
    {
    public:
        StaticMeshRenderData() = default;
        ~StaticMeshRenderData() = default;

        StaticMeshRenderData(const StaticMeshRenderData&) = delete;
        StaticMeshRenderData& operator=(const StaticMeshRenderData&) = delete;
        StaticMeshRenderData(StaticMeshRenderData&&) = delete;
        StaticMeshRenderData& operator=(StaticMeshRenderData&&) = delete;

        bool is_drawable() const
        {
            // No render-resource candidate exists before task 10.4, so the
            // skeleton must remain explicitly unavailable instead of reporting success.
            return false;
        }
    };
}
