#pragma once

#include "file_system/native_platform_file.h"
#include "rendercore/shader/shader_map_loader.h"
#include "rendercore/shader/shader_map.h"

namespace toy3d
{
    class ShaderMapEntryLoader final : public ShaderMapLoader
    {
      public:
        explicit ShaderMapEntryLoader(PhysicalPath entry_root);

        ShaderMapProgramLoadResult load_program(const ShaderMapProgramKey& key) const override;
        ShaderMapCollectionLoadResult load_collection(const std::string& shader_name, ShaderPlatform platform,
                                                      const ShaderContentHash& permutation_key) const override;
        // Validate every deployed configuration before publishing any CPU map.
        // Source defaults are resolved from the validated typed domain; explicit key queries stay exact.
        ShaderMapCollectionLoadResult load_default_collection(const std::string& shader_name,
                                                              ShaderPlatform platform) const;
        bool load_family(const std::string& shader_name, ShaderPlatform platform,
                         std::vector<ShaderMapCollectionRef>& configurations, std::string& error) const;
        bool load_deployment(std::vector<ShaderMapCollectionRef>& configurations, std::string& error) const;

      private:
        NativePlatformFile platform_file_;
        PhysicalPath entry_root_;
    };
} // namespace toy3d
