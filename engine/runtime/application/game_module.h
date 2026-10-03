#pragma once

#include "gamescene/actor/actor_type_registry.h"
#include "platform/dynamic_library.h"
#include "game_module_build.h"

namespace toy3d
{
    struct GameModuleApi
    {
        std::uint32_t abi_version = 1u;
        const char* build_identity = nullptr;
        const char* name = nullptr;
        bool (*register_types)(TypeRegistry&, ActorTypeRegistry&) = nullptr;
    };
    // Own before Engine/registries/workspace so all module callbacks die first.
    class GameModuleLibrary final
    {
      public:
        GameModuleLibrary() = default;
        ~GameModuleLibrary();
        bool load(const PhysicalPath& path, const std::string& expected_name, std::string& error);
        const GameModuleRegistration& registration() const
        {
            return registration_;
        }

      private:
        DynamicLibrary library_;
        GameModuleRegistration registration_;
        PhysicalPath loaded_copy_;
    };
} // namespace toy3d
