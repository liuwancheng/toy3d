#pragma once

#include <cstddef>
#include <string>

namespace toy3d::shader
{
    struct SourceLocation
    {
        std::string path;
        std::size_t offset = 0;
        std::size_t line = 1;
        std::size_t column = 1;
    };
} // namespace toy3d::shader
