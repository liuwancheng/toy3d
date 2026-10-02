#pragma once

#include <string>

namespace toy3d
{
    class Actor;
    class EditorCommandHistory;
    class TypeRegistry;
    void draw_actor_details(Actor& actor, EditorCommandHistory& history, const TypeRegistry& types, std::string& error);
}
