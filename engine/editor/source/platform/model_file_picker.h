#pragma once

#include <string>
#include <vector>

namespace toy3d
{
    class IWindow;

    // Model-import UI only. Cancellation returns true with an empty selection;
    // failure returns false with a message. Native handles remain in adapters.
    bool pick_model_files(IWindow& owner, std::vector<std::string>& paths, std::string& error);
}
