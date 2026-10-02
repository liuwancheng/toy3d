#pragma once

#include "rendercore/material/material_asset_builder.h"

#include <functional>
#include <map>

namespace toy3d
{
    // Application-owned GT material domain. Read-only asset identities stay
    // stable; publishing configures the entire affected inheritance graph.
    class MaterialLibrary final
    {
      public:
        MaterialLibrary(const TypeRegistry& types, const FileSystem& files, std::function<const AssetIndex&()> index,
                        std::function<std::shared_ptr<const ShaderMapProgram>(const std::string&)> programs,
                        MaterialTextureValues textures);
        AssetResult<MaterialInterfaceRef> load(const AssetRef& reference);
        AssetResult<MaterialInstanceRef> create_instance(MaterialInterfaceRef parent);
        AssetStatus release_instance(MaterialInstanceRef& instance);
        AssetStatus reload(const AssetRef& reference);
        AssetStatus prepare_shader(std::shared_ptr<const ShaderMapProgram> program);
        // Composition root enrolls its shared mesh default in the same graph
        // transaction; asset users and default users retain stable Proxy identities.
        void set_default_material(MaterialRef material)
        {
            default_material_ = std::move(material);
        }
        void set_shader_diagnostic(std::function<std::string(const std::string&)> diagnostic)
        {
            shader_diagnostic_ = std::move(diagnostic);
        }
        AssetStatus publish(bool defer_completion = false);
        void complete();
        void discard();
        // Remove scene users and drain their FIFO commands before shutdown.
        void shutdown();

      private:
        struct LoadedMaterial
        {
            AssetRef reference;
            std::shared_ptr<MaterialInterface> runtime;
            MaterialAssetData root;
            MaterialInstanceAssetData instance;
        };
        AssetStatus prepare(const AssetRef& changed);
        AssetStatus ensure_textures(const std::vector<MaterialParameterOverride>& overrides);
        AssetStatus validate_loaded_ancestors(const MaterialAssetHierarchy& hierarchy,
                                              const std::vector<AssetId>& updating) const;
        AssetStatus add_configuration(LoadedMaterial& loaded, const MaterialAssetData& root,
                                      const MaterialInstanceAssetData& instance,
                                      std::shared_ptr<const ShaderMapProgram> program, MaterialInterfaceRef parent);
        const TypeRegistry& types_;
        const FileSystem& files_;
        std::function<const AssetIndex&()> index_;
        std::function<std::shared_ptr<const ShaderMapProgram>(const std::string&)> programs_;
        MaterialTextureValues textures_;
        MaterialRef default_material_;
        std::function<std::string(const std::string&)> shader_diagnostic_;
        std::map<AssetId, LoadedMaterial> loaded_;
        std::vector<MaterialInstanceRef> temporary_;
        std::vector<MaterialInterface::Configuration> pending_;
        std::vector<MaterialInterface::Configuration> previous_;
        std::map<AssetId, std::pair<MaterialAssetData, MaterialInstanceAssetData>> pending_data_;
        bool published_ = false;
    };
} // namespace toy3d
