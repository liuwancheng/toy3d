#pragma once

#include <functional>
#include <string>
#include <vector>

namespace toy3d
{
    struct EditorPanel
    {
        std::string id;
        std::string title;
        std::string window_name;
        std::function<void()> draw;
        std::function<void()> undo;
        std::function<void()> redo;
        std::function<bool()> focused;
        std::function<void()> save;
        std::function<void()> open;
    };

    // Application owns callbacks and their dependencies through the entire UI lifetime.
    class EditorPanelRegistry
    {
      public:
        bool add(EditorPanel panel);
        void freeze() { frozen_ = true; }
        void draw();
        void draw_window_menu() const;
        void undo();
        void redo();
        void save();
        void process_shortcuts(bool blocked);
        void clear();
      private:
        const EditorPanel* history_target() const;
        std::vector<EditorPanel> panels_;
        std::string history_target_;
        bool frozen_ = false;
    };
}
