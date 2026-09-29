#pragma once

#include "asset_identity.h"
#include "hash/sha256.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/shader_map.h"
#include "material/material_asset_data.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    class World;

    struct MaterialSlotAssignment
    {
        std::uint32_t component_id = 0;
        std::string slot_name;
        AssetRef material;
    };

    constexpr const char* MATERIAL_ASSET_DRAG_PAYLOAD = "TOY3D_MATERIAL_ASSET";

    // Editor-owned asset identities and immutable loaded versions. Runtime
    // Components only hold MaterialInstanceRef; history never holds a Proxy.
    class MaterialAssignments
    {
      public:
        void initialize(EditorWorkspace& workspace, MaterialRef registered_defaults);
        void set_program_resolver(std::function<ShaderMapProgramRef(const std::string&)> resolver) { program_resolver_ = std::move(resolver); }
        bool prepare_shader(const ShaderMapProgramRef& program, std::string& error);
        bool publish_shader(std::string& error, bool defer_completion = false);
        void complete_shader();
        void discard_shader();
        bool assign(World& world, std::uint32_t actor_id, const MaterialSlotAssignment& assignment, std::string& error);
        AssetRef reference(const World& world, std::uint32_t actor_id, std::uint32_t component_id,
                           const std::string& slot_name) const;
        std::vector<MaterialSlotAssignment> capture(const World& world, std::uint32_t actor_id) const;
        void forget(std::uint32_t actor_id);
        // Call after removing scene users and draining their FIFO commands.
        void shutdown();

      private:
        struct LoadedMaterial
        {
            AssetId id;
            Sha256Hash signature{};
            MaterialInstanceRef runtime;
            Sha256Hash data_signature{};
            MaterialAssetData root;
            MaterialInstanceAssetData child;
            bool is_instance = false;
        };
        struct PendingSlot
        {
            std::uint32_t actor_id = 0u;
            MaterialSlotAssignment assignment;
            MaterialInstanceRef before;
            MaterialInstanceRef after;
        };
        MaterialInstanceRef load(const AssetRef& reference, std::string& error);
        EditorWorkspace* workspace_ = nullptr;
        World* world_ = nullptr;
        MaterialRef defaults_;
        std::vector<LoadedMaterial> loaded_;
        std::map<std::uint32_t, std::vector<MaterialSlotAssignment>> assignments_;
        std::function<ShaderMapProgramRef(const std::string&)> program_resolver_;
        std::vector<LoadedMaterial> pending_versions_;
        std::vector<PendingSlot> pending_slots_;
        std::size_t published_slots_ = 0u;
    };
}
