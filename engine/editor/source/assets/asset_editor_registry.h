#pragma once

#include "asset/asset_identity.h"
#include <functional>
#include <string>
#include <vector>

namespace toy3d
{
    struct AssetEditor
    {
        std::string root_type;
        std::function<void(const AssetId&, bool)> request_open;
    };

    // Dispatches stable asset types; the owning editor manages unsaved transitions.
    class AssetEditorRegistry
    {
      public:
        bool add(AssetEditor editor);
        void freeze() { frozen_ = true; }
        bool request_open(const std::string& root_type, const AssetId& id, bool focus) const;
        void clear() { editors_.clear(); frozen_ = false; }
      private:
        std::vector<AssetEditor> editors_;
        bool frozen_ = false;
    };
}
