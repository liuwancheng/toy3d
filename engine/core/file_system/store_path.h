#pragma once

#include "file_system/file_error.h"

#include <string>

namespace toy3d
{
    class StorePath
    {
    public:
        StorePath() = default;

        static FileResult<StorePath> parse(std::string utf8_path);
        static FileResult<StorePath> join(const StorePath& base, const StorePath& relative);

        const std::string& utf8() const;
        bool empty() const;

        friend bool operator==(const StorePath& lhs, const StorePath& rhs)
        {
            return lhs.utf8_path_ == rhs.utf8_path_;
        }

        friend bool operator!=(const StorePath& lhs, const StorePath& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        explicit StorePath(std::string utf8_path);

        std::string utf8_path_;
    };
}
