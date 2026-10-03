#pragma once

#include "asset/asset_identity.h"
#include "rendercore/material/material_library.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/shader_map.h"
#include "asset/material/material_asset_data.h"
#include "rendercore/geometry/static_mesh.h"
#include "misc/sha256.h"

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
    class ShaderWorkflow;
    class EditorCommandHistory;

    struct MaterialSlotAssignment
    {
        std::uint32_t component_id = 0;
        std::string slot_name;
        AssetRef material;
    };

    constexpr const char* MATERIAL_ASSET_DRAG_PAYLOAD = "TOY3D_MATERIAL_ASSET";

    // Editor-owned asset identities and slot assignments. Runtime
    // Components only hold MaterialInterfaceRef; history never holds a Proxy.
    class MaterialAssignments
    {
      public:
        void initialize(EditorWorkspace& workspace, MaterialLibrary& library);
        void set_shader_workflow(ShaderWorkflow& shaders)
        {
            shaders_ = &shaders;
        }
        bool offer_compile_assignment(World& world, std::uint32_t actor_id, const MaterialSlotAssignment& assignment);
        bool can_compile_assignment() const;
        bool compile_assignment(std::string& error);
        void tick_compile_assignment(World& world, EditorCommandHistory& history, std::string& error);
        AssetStatus reload(const AssetRef& reference);
        bool prepare_shader(const ShaderMapCollectionRef& program, std::string& error);
        bool prepare_shader(const std::vector<ShaderMapCollectionRef>& programs, std::string& error);
        bool publish_shader(std::string& error, bool defer_completion = false);
        void complete_shader();
        void discard_shader();
        bool assign(World& world, std::uint32_t actor_id, const MaterialSlotAssignment& assignment, std::string& error);
        AssetRef reference(const World& world, std::uint32_t actor_id, std::uint32_t component_id,
                           const std::string& slot_name) const;
        std::vector<MaterialSlotAssignment> capture(const World& world, std::uint32_t actor_id) const;
        void forget(std::uint32_t actor_id);
        // History already retains validated runtime materials; restore only author identities.
        void remember(World& world, std::uint32_t actor_id, std::vector<MaterialSlotAssignment> assignments);
        // Call after removing scene users and draining their FIFO commands.
        void shutdown();

      private:
        MaterialInterfaceRef load(const AssetRef& reference, std::string& error);
        EditorWorkspace* workspace_ = nullptr;
        World* world_ = nullptr;
        MaterialLibrary* library_ = nullptr;
        ShaderWorkflow* shaders_ = nullptr;
        struct PendingAssignment
        {
            World* world = nullptr;
            std::uint64_t generation = 0u;
            std::uint32_t actor_id = 0u;
            MaterialSlotAssignment assignment;
            std::shared_ptr<const void> mesh;
            MaterialInterfaceRef previous;
            std::string shader;
            std::map<AssetId, Sha256Hash> descriptions;
            bool compiling = false;
        };
        PendingAssignment pending_assignment_;
        std::map<std::uint32_t, std::vector<MaterialSlotAssignment>> assignments_;
    };
} // namespace toy3d
