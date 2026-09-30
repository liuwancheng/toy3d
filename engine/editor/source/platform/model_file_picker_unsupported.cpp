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
}
