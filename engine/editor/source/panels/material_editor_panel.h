#pragma once

#include "asset_identity.h"
#include "format/shader_editor_properties.h"
#include "rendercore/material/material_asset_builder.h"

#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;

    enum class MaterialCloseDecision
    {
        Save,
        Discard,
        Cancel
    };

    // Application owns the active runtime candidate. Workspace owns its
    // authoring session and history; switching is a Save/Discard/Cancel flow.
    class MaterialEditorPanel final
    {
      public:
        void initialize(EditorWorkspace& workspace, MaterialRef defaults, const PhysicalPath& shader_root);
        void request_open(const AssetId& id);
        void request_close();
        bool request_exit();
        bool take_exit();
        bool resolve_unsaved(MaterialCloseDecision decision);
        bool focused() const { return focused_; }
        bool modal_pending() const { return requested_.valid() || close_requested_ || exit_requested_; }
        void draw();
        void undo();
        void redo();
        void save();
        void shutdown();

      private:
        bool open(const AssetId& id);
        void close();
        void complete_transition();
        void report(const AssetStatus& status);
        MaterialParameterChanges parameter_changes(const std::vector<MaterialParameterOverride>& effective) const;
        void draw_parameters();

        EditorWorkspace* workspace_ = nullptr;
        MaterialRef defaults_;
        MaterialInstanceRef runtime_;
        std::vector<shader::ShaderEditorProperty> properties_;
        std::string metadata_warning_;
        std::string error_;
        AssetId requested_;
        bool close_requested_ = false;
        bool exit_requested_ = false;
        bool exit_ready_ = false;
        bool focused_ = false;
        bool focus_requested_ = false;
    };
}
