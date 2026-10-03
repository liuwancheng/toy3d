#pragma once

#include "asset/asset_identity.h"
#include "assets/material/material_edit_session.h"
#include <memory>
#include "shader/shader_editor_properties.h"
#include "rendercore/material/material_asset_builder.h"
#include "rendercore/material/material_shader_map_validation.h"
#include "rendercore/shader/shader_map.h"

#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    class ShaderWorkflow;
    class AssetThumbnailPool;

    enum class MaterialCloseDecision
    {
        Save,
        Discard,
        Cancel
    };

    // The asset editor owns its author session, history and runtime preview.
    // Workspace provides file/catalog services; switching uses Save/Discard/Cancel.
    class MaterialEditorPanel final
    {
      public:
        void initialize(EditorWorkspace& workspace, MaterialRef defaults, const PhysicalPath& shader_root);
        MaterialEditSession& edit_session()
        {
            return *session_;
        }
        const MaterialEditSession& edit_session() const
        {
            return *session_;
        }
        void set_preview_pool(AssetThumbnailPool& previews)
        {
            previews_ = &previews;
        }
        void set_shader_workflow(ShaderWorkflow& workflow);
        bool prepare_shader(const ShaderMapCollectionRef& program,
                            const std::vector<shader::ShaderEditorProperty>& properties, std::string& error);
        bool prepare_shader(const std::vector<ShaderMapCollectionRef>& programs,
                            const std::vector<shader::ShaderEditorProperty>& properties, std::string& error);
        void publish_shader();
        void discard_shader();
        bool collect_shader_validation_targets(const std::vector<ShaderMapCollectionRef>& programs,
                                               std::vector<MaterialShaderMapValidationTarget>& targets,
                                               std::string& error) const;
        std::uint64_t session_revision() const
        {
            return session_revision_;
        }
        void request_open(const AssetId& id);
        void request_close();
        bool request_exit();
        bool take_exit();
        bool resolve_unsaved(MaterialCloseDecision decision);
        bool focused() const
        {
            return focused_;
        }
        bool modal_pending() const
        {
            return requested_.valid() || close_requested_ || exit_requested_;
        }
        void draw();
        void set_static_option(const MaterialStaticOption& value);
        void remove_static_option(const std::string& name);
        void undo();
        void redo();
        void save();
        AssetId take_locate_parent()
        {
            const auto id = locate_parent_;
            locate_parent_ = {};
            return id;
        }
        void shutdown();

      private:
        AssetResult<MaterialInstanceRef> build_preview_material(const MaterialAssetData& data,
                                                                ShaderMapCollectionRef program);
        bool open(const AssetId& id);
        void close();
        void complete_transition();
        void report(const AssetStatus& status);
        AssetStatus ensure_texture_values(const std::vector<MaterialParameterOverride>& values);
        MaterialParameterChanges parameter_changes(const std::vector<MaterialParameterOverride>& effective) const;
        void draw_parameters();
        void draw_preview();
        void draw_static_options();
        void request_static_configuration();
        AssetStatus validate_static_preview() const;

        std::unique_ptr<MaterialEditSession> session_;
        EditorWorkspace* workspace_ = nullptr;
        MaterialRef defaults_;
        MaterialTextureValues textures_;
        MaterialInstanceRef runtime_;
        MaterialInstanceRef shader_candidate_;
        std::vector<shader::ShaderEditorProperty> candidate_properties_;
        shader::ShaderParameterSchema candidate_schema_;
        shader::ShaderPermutationDomain candidate_static_domain_;
        ShaderWorkflow* shaders_ = nullptr;
        AssetThumbnailPool* previews_ = nullptr;
        std::uint64_t preview_revision_ = 0u;
        std::uint64_t session_revision_ = 0u;
        std::vector<shader::ShaderEditorProperty> properties_;
        std::string metadata_warning_;
        std::string error_;
        AssetId requested_;
        AssetId locate_parent_;
        bool close_requested_ = false;
        bool exit_requested_ = false;
        bool exit_ready_ = false;
        bool pending_save_failed_ = false;
        bool focused_ = false;
        bool focus_requested_ = false;
        bool static_recompile_pending_ = false;
    };
} // namespace toy3d
