#include "file_system/store_path.h"

#include <cstdint>
#include <utility>

#include "misc/utf8.h"

namespace toy3d
{
    namespace
    {
        FileStatus invalid_store_path(const std::string& path, const char* message)
        {
            FileStatus status;
            status.code = FileErrorCode::InvalidPath;
            status.operation = "parse_store_path";
            status.message = message;
            status.virtual_path = path;
            return status;
        }

    } // namespace

    StorePath::StorePath(std::string utf8_path) : utf8_path_(std::move(utf8_path))
    {
    }

    FileResult<StorePath> StorePath::parse(std::string utf8_path)
    {
        if (!is_valid_utf8(utf8_path) || utf8_path.find('\0') != std::string::npos)
        {
            return FileResult<StorePath>(
                invalid_store_path(utf8_path, "store path must be valid UTF-8 without embedded nulls"));
        }
        if ((!utf8_path.empty() && utf8_path.front() == '/') || utf8_path.find('\\') != std::string::npos)
        {
            return FileResult<StorePath>(
                invalid_store_path(utf8_path, "store path must be relative and use '/' separators"));
        }
        if (!utf8_path.empty() && utf8_path.back() == '/')
        {
            utf8_path.pop_back();
            if (!utf8_path.empty() && utf8_path.back() == '/')
            {
                return FileResult<StorePath>(
                    invalid_store_path(utf8_path, "store path must not contain empty segments"));
            }
        }

        std::size_t segment_start = 0;
        while (segment_start < utf8_path.size())
        {
            const std::size_t separator = utf8_path.find('/', segment_start);
            const std::size_t segment_end = separator == std::string::npos ? utf8_path.size() : separator;
            if (segment_end == segment_start)
            {
                return FileResult<StorePath>(
                    invalid_store_path(utf8_path, "store path must not contain empty segments"));
            }
            const std::string segment = utf8_path.substr(segment_start, segment_end - segment_start);
            if (segment == "." || segment == "..")
            {
                return FileResult<StorePath>(invalid_store_path(utf8_path, "store path must not contain dot segments"));
            }
            if (separator == std::string::npos)
            {
                break;
            }
            segment_start = separator + 1;
        }
        return FileResult<StorePath>(StorePath(std::move(utf8_path)));
    }

    FileResult<StorePath> StorePath::join(const StorePath& base, const StorePath& relative)
    {
        if (base.empty())
        {
            return FileResult<StorePath>(relative);
        }
        if (relative.empty())
        {
            return FileResult<StorePath>(base);
        }
        return parse(base.utf8() + "/" + relative.utf8());
    }

    const std::string& StorePath::utf8() const
    {
        return utf8_path_;
    }

    bool StorePath::empty() const
    {
        return utf8_path_.empty();
    }
} // namespace toy3d
