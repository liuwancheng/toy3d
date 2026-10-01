#include "file_system/physical_path.h"

#include <cstdint>
#include <utility>

#include "misc/utf8.h"

namespace toy3d
{
    PhysicalPath::PhysicalPath(std::string utf8_path)
        : utf8_path_(std::move(utf8_path)),
          valid_(utf8_path_.find('\0') == std::string::npos && is_valid_utf8(utf8_path_))
    {
    }

    const std::string& PhysicalPath::utf8() const
    {
        return utf8_path_;
    }

    bool PhysicalPath::empty() const
    {
        return utf8_path_.empty();
    }

    bool PhysicalPath::valid() const
    {
        return valid_;
    }
} // namespace toy3d
