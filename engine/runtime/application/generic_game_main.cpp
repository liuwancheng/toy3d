#include "application/game_host.h"
#include "config/command_line_parser.h"

namespace toy3d
{
    GameModuleRegistration linked_game_module()
    {
        return {"", [](TypeRegistry&, ActorTypeRegistry&)
                {
                    return true;
                }};
    }
} // namespace toy3d
