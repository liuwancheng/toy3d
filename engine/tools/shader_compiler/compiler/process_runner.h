#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace toy3d::shader
{
    struct ProcessResult
    {
        bool launched = false;
        int exit_code = -1;
        std::string output;
    };

    ProcessResult run_process(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments);
}
