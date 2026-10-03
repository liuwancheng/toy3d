#pragma once

#include <string>
#include <vector>

namespace toy3d
{
    class IWindow;

    // Native Editor selection. Cancellation returns true with an empty selection;
    // failure returns false with a message. Native handles remain in adapters.
    bool pick_project_file(IWindow& owner, std::string& path, std::string& error);
    bool pick_project_folder(IWindow& owner, std::string& path, std::string& error);
    bool pick_model_files(IWindow& owner, std::vector<std::string>& paths, std::string& error);
    bool pick_environment_files(IWindow& owner, std::vector<std::string>& paths, std::string& error);
    bool pick_texture_files(IWindow& owner, std::vector<std::string>& paths, std::string& error);
} // namespace toy3d
