#pragma once

#include "asset_file.h"

#include <map>
#include <string>
#include <vector>

namespace toy3d
{
    struct AssetLocation
    {
        VirtualPath path;
        AssetFileIndex index;
    };

    // The composition root owns this index and serializes its mutations.
    class AssetIndex
    {
      public:
        AssetStatus add(const VirtualPath& path, const AssetFileIndex& index);
        AssetStatus move(const AssetId& id, const VirtualPath& new_path);
        const AssetLocation* find(const AssetId& id) const;
        AssetStatus resolve(const AssetRef& reference, const std::string& property_path = {}) const;
        AssetStatus validate_strong_dependencies() const;

      private:
        std::map<AssetId, AssetLocation> by_id_;
        std::map<std::string, AssetId> by_path_;
    };

    struct ImportedSubresource
    {
        std::string source_key;
        SubresourceId id;
    };

    struct SubresourceMatch
    {
        std::vector<ImportedSubresource> matched;
        std::vector<ImportedSubresource> orphaned;
        std::vector<std::string> new_source_keys;
    };

    AssetResult<SubresourceMatch> match_subresources(const std::vector<ImportedSubresource>& previous,
                                                     const std::vector<std::string>& imported_source_keys);
} // namespace toy3d
