#pragma once

#include "file_system/physical_path.h"
#include "gamescene/actor/actor_type_registry.h"

namespace toy3d
{
    struct GameHostPaths
    {
        PhysicalPath descriptor;
        PhysicalPath deployment;
        PhysicalPath engine_assets;
        PhysicalPath engine_config;
    };
    // The entry point supplies the module; no project singleton or dynamic library.
    int run_game_host(const GameHostPaths& paths, const GameModuleRegistration& module, void* native_instance);
    GameModuleRegistration linked_game_module();
}
