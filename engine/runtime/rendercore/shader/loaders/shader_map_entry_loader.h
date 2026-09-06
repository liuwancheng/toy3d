#pragma once

#include "file_system/native_platform_file.h"
#include "rendercore/shader/shader_map_loader.h"

namespace toy3d
{
    class ShaderMapEntryLoader final : public ShaderMapLoader
    {
      public:
        explicit ShaderMapEntryLoader(PhysicalPath entry_root);

        ShaderMapProgramLoadResult load_program(const ShaderMapProgramKey& key) const override;

      private:
        NativePlatformFile platform_file_;
        PhysicalPath entry_root_;
    };
} // namespace toy3d
