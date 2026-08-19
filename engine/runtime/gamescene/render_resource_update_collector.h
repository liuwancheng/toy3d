#pragma once

#include "rendercore/render_resource_update.h"

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace toy3d
{
    class World;

    class RenderResourceUpdateCollector final
    {
    public:
        // The caller supplies every active World in the global frame. This lets
        // one device-level collector keep shared resources alive across Worlds.
        std::vector<RenderResourceUpdate> collect(
            const std::vector<std::reference_wrapper<const World>>& worlds);

    private:
        friend class World;

        void add_mesh(const StaticMeshRef& mesh);
        void add_material(const MaterialInstanceRef& material);

        std::map<std::uint64_t, StaticMeshRef> current_meshes_;
        std::map<std::uint64_t, MaterialInstanceRef> current_materials_;
        std::map<std::uint64_t, RenderResourceRevision> published_meshes_;
        std::map<std::uint64_t, RenderResourceRevision> published_materials_;
    };
}
