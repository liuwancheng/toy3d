#pragma once

#include <string>

namespace toy3d
{
    class PhysicalPath
    {
    public:
        PhysicalPath() = default;
        explicit PhysicalPath(std::string utf8_path);

        const std::string& utf8() const;
        bool empty() const;
        bool valid() const;

        friend bool operator==(const PhysicalPath& lhs, const PhysicalPath& rhs)
        {
            return lhs.utf8_path_ == rhs.utf8_path_;
        }

        friend bool operator!=(const PhysicalPath& lhs, const PhysicalPath& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        std::string utf8_path_;
        bool valid_ = true;
    };
}
