#pragma once

#include "file_system/physical_path.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/shader/shader_map.h"

namespace toy3d
{
    // Shared CPU geometry and default Material; released after World unregisters.
    class SceneGeometry
    {
      public:
        bool initialize(const PhysicalPath& shader_entries, ShaderMapProgramRef program = {});
        void release();
        StaticMeshRef instantiate(const std::string& kind) const;
        const MaterialInstanceRef& default_material() const { return material_; }
      private:
        StaticMeshRef cube_;
        StaticMeshRef plane_;
        MaterialInstanceRef material_;
    };
    StaticMeshRef clone_scene_geometry(const StaticMeshRef& prototype);
}
