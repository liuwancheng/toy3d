#pragma once

#include "math/length_units.h"

#include <array>
#include <string>
#include <vector>

namespace toy3d
{
    class IWindow;
    class EditorWorkspace;
    class EditorSelection;
    class AssetThumbnailPool;

    // UI preflight; importer and atomic CreateNew remain the authority at publish.
    bool static_mesh_import_destination(const std::string& source, const std::string& folder, const std::string& name,
                                        float scale, std::string& destination, std::string& error);

    class StaticMeshImportDialog
    {
      public:
        bool request(const std::string& folder, const std::vector<std::string>& sources = {});
        void draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection,
                  AssetThumbnailPool& thumbnails);
        bool active() const
        {
            return active_;
        }
        const std::string& error() const
        {
            return error_;
        }
        void clear();

      private:
        struct Candidate
        {
            std::array<char, 4097> source{};
            std::array<char, 256> name{};
            std::string error;
            float source_unit_in_centimeters = k_centimeters_per_meter;
            bool use_file_unit = false;
            bool is_fbx = false;
        };
        void suggest_source_unit(Candidate& candidate);
        void set_sources(const std::vector<std::string>& sources);
        std::vector<Candidate> candidates_;
        std::string folder_;
        std::string error_;
        float scale_ = 1.0f;
        bool convert_scene_unit_ = true;
        bool active_ = false;
        bool open_ = false;
        bool browse_ = false;
    };
} // namespace toy3d
