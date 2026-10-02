#pragma once

#include "file_system/file_error.h"

#include <string>

namespace toy3d
{
    class VirtualPath
    {
      public:
        VirtualPath() = default;

        static FileResult<VirtualPath> parse(std::string utf8_path);

        const std::string& utf8() const;
        bool empty() const;
        bool is_root() const;

        friend bool operator==(const VirtualPath& lhs, const VirtualPath& rhs)
        {
            return lhs.utf8_path_ == rhs.utf8_path_;
        }

        friend bool operator!=(const VirtualPath& lhs, const VirtualPath& rhs)
        {
            return !(lhs == rhs);
        }

      private:
        explicit VirtualPath(std::string utf8_path);

        std::string utf8_path_;
    };
} // namespace toy3d
