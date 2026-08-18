#pragma once

#include "file_system/physical_path.h"

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
        const PhysicalPath& executable,
        const std::vector<std::string>& arguments);
}
