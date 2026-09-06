#pragma once

#include "file_system/physical_path.h"
#include "rendercore/shader/shader_map_loader.h"

namespace toy3d
{
    class ShaderCodeLibraryLoader final : public ShaderMapLoader
    {
      public:
        explicit ShaderCodeLibraryLoader(PhysicalPath library_path);

        ShaderMapProgramLoadResult load_program(const ShaderMapProgramKey& key) const override;

      private:
        PhysicalPath library_path_;
    };
} // namespace toy3d
