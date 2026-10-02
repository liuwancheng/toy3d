#include "platform/model_file_picker.h"

namespace toy3d
{
    bool pick_model_files(IWindow&, std::vector<std::string>& paths, std::string& error)
    {
        paths.clear();
        error = "Native model selection is unsupported on this platform. Enter a source file path instead.";
        return false;
    }
    bool pick_texture_files(IWindow&, std::vector<std::string>& paths, std::string& error)
    {
        paths.clear();
        error = "Native texture selection is unsupported on this platform. Enter a source file path instead.";
        return false;
    }
    bool pick_project_file(IWindow&, std::string& path, std::string& error)
    { path.clear(); error = "Native project selection is unsupported. Launch with --Project=<file.toy>."; return false; }
    bool pick_project_folder(IWindow&, std::string& path, std::string& error)
    { path.clear(); error = "Native folder selection is unsupported. Enter a parent folder path."; return false; }

}
