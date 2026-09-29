#pragma once

#include "edit_session.h"
#include "material/material_asset.h"

#include <functional>
#include <memory>

namespace toy3d
{
    class EditorWorkspace;

    // Workspace owns one authoring session. Runtime values and GPU ownership
    // belong to the injected preview adapter, never to the persisted snapshots.
    class MaterialEditSession final
    {
      public:
        using PreviewPrepare = std::function<AssetStatus(const std::vector<MaterialParameterOverride>&)>;
        using PreviewNotify = std::function<void(const std::vector<MaterialParameterOverride>&)>;

        explicit MaterialEditSession(EditorWorkspace& workspace) : workspace_(workspace) {}
        AssetStatus open(const AssetId& id, shader::ShaderParameterSchema schema,
                         const std::string& registered_shader_name = "Toy3d/Surface/Phong");
        AssetStatus update_schema(shader::ShaderParameterSchema schema);
        void set_preview(PreviewPrepare prepare, PreviewNotify notify);
        void clear();
        bool active() const { return root_ || instance_; }
        bool is_instance() const { return instance_ != nullptr; }
        bool writable() const { return active() && path_.utf8().compare(0, 9, "/Project/") == 0; }
        bool dirty() const;
        bool gesturing() const { return gesture_active_; }
        std::size_t undo_count() const;
        std::size_t redo_count() const;
        const AssetId& id() const { return id_; }
        const VirtualPath& path() const { return path_; }
        const MaterialAssetData& root_data() const;
        const MaterialInstanceAssetData* instance_data() const;
        const shader::ShaderParameterSchema& schema() const { return schema_; }
        const std::vector<MaterialParameterOverride>& overrides() const;
        std::vector<MaterialParameterOverride> effective_overrides() const;
        std::vector<MaterialParameterOverride> effective_overrides(const shader::ShaderParameterSchema& schema) const;
        AssetStatus begin_gesture();
        AssetStatus set_parameter(const MaterialParameterOverride& value);
        AssetStatus remove_parameter(const std::string& name);
        AssetStatus finish_gesture();
        AssetStatus cancel_gesture();
        AssetStatus undo();
        AssetStatus redo();
        AssetStatus save();

      private:
        AssetStatus validate_overrides(const std::vector<MaterialParameterOverride>& values) const;
        std::vector<MaterialParameterOverride> effective(const std::vector<MaterialParameterOverride>& values) const;
        AssetStatus effective_bytes(const std::vector<MaterialParameterOverride>& values, std::vector<std::uint8_t>& bytes) const;
        AssetStatus prepare(const std::vector<MaterialParameterOverride>& values) const;
        void notify(const std::vector<MaterialParameterOverride>& values) const;
        AssetStatus commit(std::vector<MaterialParameterOverride> values);
        AssetStatus fail(AssetErrorCode code, const std::string& message) const;

        EditorWorkspace& workspace_;
        AssetId id_;
        VirtualPath path_;
        AssetFileIndex opened_index_;
        shader::ShaderParameterSchema schema_;
        MaterialAssetData parent_;
        std::unique_ptr<EditSession<MaterialAssetData>> root_;
        std::unique_ptr<EditSession<MaterialInstanceAssetData>> instance_;
        std::vector<MaterialParameterOverride> draft_;
        std::vector<MaterialParameterOverride> gesture_before_;
        bool gesture_active_ = false;
        PreviewPrepare preview_prepare_;
        PreviewNotify preview_notify_;
    };
}
