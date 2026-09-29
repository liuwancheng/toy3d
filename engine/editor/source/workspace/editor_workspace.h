#pragma once

#include "asset_catalog.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "file_system/physical_path.h"
#include "reflection/type_registry.h"
#include "material/material_edit_session.h"
#include "format/shader_editor_properties.h"

#include <string>

namespace toy3d
{
    struct EditorWorkspacePaths
    {
        PhysicalPath project_assets;
        PhysicalPath engine_assets;
        PhysicalPath editor_resources;
        PhysicalPath deployment;
    };

    class EditorWorkspace final
    {
      public:
        bool initialize(const EditorWorkspacePaths& paths);
        bool refresh();

        const AssetCatalog& catalog() const { return catalog_; }
        const std::string& error() const { return error_; }
        const PhysicalPath& source_root() const { return source_root_; }
        FileSystem& files() { return files_; }
        const TypeRegistry& types() const { return types_; }
        bool ready() const { return ready_; }
        MaterialEditSession& material_edit() { return material_edit_; }
        bool read_material_properties(const PhysicalPath& registered_root, const std::string& shader_name,
            const shader::ShaderParameterSchema& schema, std::vector<shader::ShaderEditorProperty>& properties,
            std::string& error) const;

      private:
        NativePlatformFile platform_file_;
        FileSystem files_;
        PhysicalPath source_root_;
        AssetCatalog catalog_;
        TypeRegistry types_;
        MaterialEditSession material_edit_{*this};
        std::string error_;
        bool ready_ = false;
    };
} // namespace toy3d
