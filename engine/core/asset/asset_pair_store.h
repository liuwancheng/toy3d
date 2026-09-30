#pragma once

#include "asset_pair.h"

#include <mutex>

namespace toy3d
{
    // The Editor workspace owns one store and serializes asset publication through it.
    class AssetPairStore
    {
      public:
        AssetPairStore(const TypeRegistry& types, FileSystem& files);

        AssetStatus publish(const VirtualPath& path, const AssetPairBytes& pair,
            FilePublishMode mode);
        AssetStatus remove(const VirtualPath& path);
        AssetResult<AssetId> copy(const VirtualPath& source, const VirtualPath& destination);
        AssetStatus move(const VirtualPath& source, const VirtualPath& destination);
        AssetResult<AssetPair> read(const VirtualPath& path);
        AssetStatus recover(const VirtualPath& path);
        AssetStatus recover_tree(const VirtualPath& root);

      private:
        AssetStatus recover_locked(const VirtualPath& path);
        AssetStatus recover_tree_locked(const VirtualPath& root, std::size_t depth);
        AssetStatus recover_moves_tree(const VirtualPath& root, std::size_t depth);
        AssetStatus recover_move(const VirtualPath& source);

        const TypeRegistry& types_;
        FileSystem& files_;
        std::mutex mutex_;
    };
}
