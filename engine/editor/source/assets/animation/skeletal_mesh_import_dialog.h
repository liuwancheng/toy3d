#pragma once

#include "asset/asset_identity.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    class EditorSelection;
    class AssetThumbnailPool;
    class IWindow;
    class SkeletalImportJob;

    class SkeletalMeshImportDialog final
    {
      public:
        SkeletalMeshImportDialog();
        ~SkeletalMeshImportDialog();
        bool request(const std::string& folder, bool animation_only = false,
                     const std::vector<std::string>& sources = {});
        bool request_reimport(EditorWorkspace& workspace, const AssetId& id);
        void draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection,
                  AssetThumbnailPool& thumbnails);
        bool active() const;
        const std::string& error() const;
        void clear();
        void shutdown();

      private:
        void set_source(const std::string& source);
        std::unique_ptr<SkeletalImportJob> job_;
        std::array<char, 4097> source_{};
        std::array<char, 256> name_{};
        std::string folder_;
        std::string error_;
        AssetId skeleton_id_;
        AssetId reimport_id_;
        std::string reimport_path_;
        float scale_ = 1.0f;
        float source_unit_ = 100.0f;
        bool file_units_ = false;
        bool convert_units_ = true;
        bool reduce_influences_ = false;
        bool sample_at_60_ = false;
        bool animation_only_ = false;
        bool active_ = false;
        bool open_ = false;
        bool browse_ = false;
        bool committed_ = false;
    };
} // namespace toy3d
