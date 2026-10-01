#pragma once

#include "asset_catalog.h"
#include "asset_pair_store.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "file_system/physical_path.h"
#include "reflection/type_registry.h"
#include "format/shader_editor_properties.h"

#include <string>
#include <memory>

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
        AssetPairStore& asset_pairs() { return *asset_pairs_; }
        AssetStatus delete_asset(const AssetId& id);
        AssetStatus move_asset(const AssetId& id, const VirtualPath& destination);
        AssetResult<AssetId> copy_asset(const AssetId& id, const VirtualPath& destination);
        bool read_material_properties(const PhysicalPath& registered_root, const std::string& shader_name,
            const shader::ShaderParameterSchema& schema, std::vector<shader::ShaderEditorProperty>& properties,
            std::string& error) const;

      private:
        NativePlatformFile platform_file_;
        FileSystem files_;
        PhysicalPath source_root_;
        AssetCatalog catalog_;
        TypeRegistry types_;
        std::unique_ptr<AssetPairStore> asset_pairs_;
        std::string error_;
        bool ready_ = false;
    };
} // namespace toy3d
