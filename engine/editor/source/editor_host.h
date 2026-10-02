#pragma once

#include "gamescene/actor/actor_type_registry.h"
#include "file_system/physical_path.h"

namespace toy3d
{
    struct EditorHostConfig
    {
        GameModuleRegistration module;
        PhysicalPath game_executable;
    };
    int run_editor_host(void* native_instance, const EditorHostConfig& config = {});
    GameModuleRegistration linked_game_module();
} // namespace toy3d
