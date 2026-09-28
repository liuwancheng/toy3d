#pragma once

#include <string>

namespace toy3d
{
    class EditorSelection;
    class EditorCommandHistory;
    class EditorWorkspace;
    class World;
    class ActorFactory;

    bool draw_outliner(World& world, EditorSelection& selection, EditorCommandHistory& history, const ActorFactory& factory);
    void draw_details(World& world, EditorSelection& selection, EditorCommandHistory& history,
                      const EditorWorkspace& workspace);
    void draw_content_browser(EditorWorkspace& workspace, EditorSelection& selection, std::string& folder,
                              bool& show_engine_content);
} // namespace toy3d
