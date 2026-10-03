#pragma once

#include "asset_pipeline/skeletal_mesh_import.h"
#include "file_system/physical_path.h"
#include "threading/task_graph/graph_event.h"

namespace toy3d
{
    class EditorWorkspace;

    enum class SkeletalImportMode
    {
        MeshAndAnimations,
        AnimationOnly
    };

    struct SkeletalImportRequest
    {
        PhysicalPath source;
        std::string destination;
        SkeletalImportMode mode = SkeletalImportMode::MeshAndAnimations;
        SkeletalMeshImportOptions options;
        AssetId skeleton_id;
        SkeletonAssetData skeleton;
        std::string skeleton_path;
        std::vector<std::uint8_t> skeleton_baseline;
        AssetId reimport_id;
        std::vector<std::uint8_t> target_baseline;
        std::string workspace_root;
    };

    struct SkeletalImportOutput
    {
        AssetId id;
        VirtualPath path;
        AssetPairBytes bytes;
    };

    struct PreparedSkeletalImport
    {
        std::vector<SkeletalImportOutput> outputs;
        std::vector<std::string> warnings;
        PhysicalPath source_root;
        std::vector<ImportedModelSource> sources;
        std::string error;
    };

    // GT captures dependency/target baselines. Workers borrow neither workspace nor UI.
    bool capture_skeletal_import(EditorWorkspace& workspace, const PhysicalPath& source, const std::string& destination,
                                 SkeletalImportMode mode, const AssetId& skeleton_id,
                                 const SkeletalMeshImportOptions& options, const AssetId& reimport_id,
                                 SkeletalImportRequest& request, std::string& error);
    bool prepare_skeletal_import(const SkeletalImportRequest& request, PreparedSkeletalImport& result);
    // GT revalidates all inputs before the first commit. committed remains populated on partial failure.
    bool publish_skeletal_import(EditorWorkspace& workspace, const SkeletalImportRequest& request,
                                 const PreparedSkeletalImport& result, std::vector<AssetId>& committed,
                                 std::string& error);

    // One owned worker at a time; cancel drops its publication authority, shutdown joins it.
    class SkeletalImportJob final
    {
      public:
        SkeletalImportJob() = default;
        SkeletalImportJob(const SkeletalImportJob&) = delete;
        SkeletalImportJob& operator=(const SkeletalImportJob&) = delete;
        ~SkeletalImportJob();
        bool start(SkeletalImportRequest request, std::string& error);
        bool update(EditorWorkspace& workspace);
        void cancel();
        void shutdown();
        bool busy() const;
        const std::string& error() const;
        const std::vector<AssetId>& committed() const;
        const std::vector<std::string>& warnings() const;

      private:
        SkeletalImportRequest request_;
        std::shared_ptr<PreparedSkeletalImport> result_;
        GraphEventRef task_;
        std::vector<AssetId> committed_;
        std::vector<std::string> warnings_;
        std::string error_;
        bool cancelled_ = false;
    };
} // namespace toy3d
