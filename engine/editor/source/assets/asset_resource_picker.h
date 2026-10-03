#pragma once

#include "assets/thumbnails/asset_thumbnail_pool.h"

#include <array>

namespace toy3d
{
    class EditorWorkspace;
    struct AssetResourceSelection
    {
        AssetId asset;
        std::string builtin;
    };

    // Shared editor widget. The caller validates and commits resources; this owns only search state.
    class AssetResourcePicker final
    {
      public:
        explicit AssetResourcePicker(AssetThumbnailPool& thumbnails);
        bool draw(const char* label, const EditorWorkspace& workspace, const AssetResourceSelection& current,
                  const std::vector<std::string>& types, AssetResourceSelection& selected, std::string& error,
                  const std::function<bool(const AssetCatalogEntry&)>& filter = {}, bool builtins = false);
        void set_browse(std::function<void(const AssetId&)> browse);
        void set_builtin_resolver(std::function<StaticMeshRef(const std::string&)> resolver);
        void clear();

      private:
        AssetThumbnailPool& thumbnails_;
        std::map<unsigned int, std::array<char, 128>> searches_;
        std::function<void(const AssetId&)> browse_;
        std::function<StaticMeshRef(const std::string&)> builtin_resolver_;
        std::map<std::string, StaticMeshRef> builtins_;
    };
} // namespace toy3d
