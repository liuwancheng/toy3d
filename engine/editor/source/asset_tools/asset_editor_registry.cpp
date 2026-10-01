#include "asset_tools/asset_editor_registry.h"

#include <utility>
#include "logging/logger.h"

namespace toy3d
{
    bool AssetEditorRegistry::add(AssetEditor editor)
    {
        if (frozen_ || editor.root_type.empty() || !editor.request_open) return false;
        for (const auto& current : editors_)
            if (current.root_type == editor.root_type) return false;
        editors_.push_back(std::move(editor));
        return true;
    }

    bool AssetEditorRegistry::request_open(const std::string& root_type, const AssetId& id, bool focus) const
    {
        if (!id.valid()) return false;
        for (const auto& editor : editors_)
            if (editor.root_type == root_type) { editor.request_open(id, focus); return true; }
        TOY_LOG_WARN("No asset editor registered for {}.", root_type);
        return false;
    }
}
