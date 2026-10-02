#include "file_system/virtual_path.h"

#include <cstdint>
#include <utility>

#include "misc/utf8.h"

namespace toy3d
{
    namespace
    {
        FileStatus invalid_virtual_path(const std::string& path, const char* message)
        {
            FileStatus status;
            status.code = FileErrorCode::InvalidPath;
            status.operation = "parse_virtual_path";
            status.virtual_path = path;
            status.message = message;
            return status;
        }

    } // namespace

    VirtualPath::VirtualPath(std::string utf8_path) : utf8_path_(std::move(utf8_path))
    {
    }

    FileResult<VirtualPath> VirtualPath::parse(std::string utf8_path)
    {
        if (utf8_path.empty() || utf8_path.front() != '/')
        {
            return FileResult<VirtualPath>(
                invalid_virtual_path(utf8_path, "virtual path must be absolute and start with '/'"));
        }
        if (!is_valid_utf8(utf8_path) || utf8_path.find('\0') != std::string::npos)
        {
            return FileResult<VirtualPath>(
                invalid_virtual_path(utf8_path, "virtual path must be valid UTF-8 without embedded nulls"));
        }
        if (utf8_path.find('\\') != std::string::npos)
        {
            return FileResult<VirtualPath>(invalid_virtual_path(utf8_path, "virtual path must use '/' separators"));
        }
        if (utf8_path.size() > 1 && utf8_path.back() == '/')
        {
            utf8_path.pop_back();
            if (utf8_path.back() == '/')
            {
                return FileResult<VirtualPath>(
                    invalid_virtual_path(utf8_path, "virtual path must not contain empty segments"));
            }
        }

        std::size_t segment_start = 1;
        while (segment_start < utf8_path.size())
        {
            const std::size_t separator = utf8_path.find('/', segment_start);
            const std::size_t segment_end = separator == std::string::npos ? utf8_path.size() : separator;
            if (segment_end == segment_start)
            {
                return FileResult<VirtualPath>(
                    invalid_virtual_path(utf8_path, "virtual path must not contain empty segments"));
            }
            const std::string segment = utf8_path.substr(segment_start, segment_end - segment_start);
            if (segment == "." || segment == "..")
            {
                return FileResult<VirtualPath>(
                    invalid_virtual_path(utf8_path, "virtual path must not contain dot segments"));
            }
            if (separator == std::string::npos)
            {
                break;
            }
            segment_start = separator + 1;
        }
        return FileResult<VirtualPath>(VirtualPath(std::move(utf8_path)));
    }

    const std::string& VirtualPath::utf8() const
    {
        return utf8_path_;
    }

    bool VirtualPath::empty() const
    {
        return utf8_path_.empty();
    }

    bool VirtualPath::is_root() const
    {
        return utf8_path_ == "/";
    }
} // namespace toy3d
