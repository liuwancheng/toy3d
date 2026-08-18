#include "file_system/store_path.h"

#include <cstdint>
#include <utility>

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

        bool is_valid_utf8(const std::string& text)
        {
            std::size_t index = 0;
            while (index < text.size())
            {
                const auto lead = static_cast<unsigned char>(text[index]);
                std::size_t continuation_count = 0;
                std::uint32_t code_point = 0;
                if (lead <= 0x7f)
                {
                    ++index;
                    continue;
                }
                if ((lead & 0xe0u) == 0xc0u)
                {
                    continuation_count = 1;
                    code_point = lead & 0x1fu;
                }
                else if ((lead & 0xf0u) == 0xe0u)
                {
                    continuation_count = 2;
                    code_point = lead & 0x0fu;
                }
                else if ((lead & 0xf8u) == 0xf0u)
                {
                    continuation_count = 3;
                    code_point = lead & 0x07u;
                }
                else
                {
                    return false;
                }
                if (index + continuation_count >= text.size())
                {
                    return false;
                }
                for (std::size_t offset = 1; offset <= continuation_count; ++offset)
                {
                    const auto next = static_cast<unsigned char>(text[index + offset]);
                    if ((next & 0xc0u) != 0x80u)
                    {
                        return false;
                    }
                    code_point = (code_point << 6u) | (next & 0x3fu);
                }
                const bool overlong =
                    (continuation_count == 1 && code_point < 0x80u) ||
                    (continuation_count == 2 && code_point < 0x800u) ||
                    (continuation_count == 3 && code_point < 0x10000u);
                if (overlong || code_point > 0x10ffffu ||
                    (code_point >= 0xd800u && code_point <= 0xdfffu))
                {
                    return false;
                }
                index += continuation_count + 1;
            }
            return true;
        }
    }

    StorePath::StorePath(std::string utf8_path)
        : utf8_path_(std::move(utf8_path))
    {
    }

    FileResult<StorePath> StorePath::parse(std::string utf8_path)
    {
        if (!is_valid_utf8(utf8_path) || utf8_path.find('\0') != std::string::npos)
        {
            return FileResult<StorePath>(invalid_store_path(
                utf8_path, "store path must be valid UTF-8 without embedded nulls"));
        }
        if ((!utf8_path.empty() && utf8_path.front() == '/') ||
            utf8_path.find('\\') != std::string::npos)
        {
            return FileResult<StorePath>(invalid_store_path(
                utf8_path, "store path must be relative and use '/' separators"));
        }
        if (!utf8_path.empty() && utf8_path.back() == '/')
        {
            utf8_path.pop_back();
            if (!utf8_path.empty() && utf8_path.back() == '/')
            {
                return FileResult<StorePath>(invalid_store_path(
                    utf8_path, "store path must not contain empty segments"));
            }
        }

        std::size_t segment_start = 0;
        while (segment_start < utf8_path.size())
        {
            const std::size_t separator = utf8_path.find('/', segment_start);
            const std::size_t segment_end =
                separator == std::string::npos ? utf8_path.size() : separator;
            if (segment_end == segment_start)
            {
                return FileResult<StorePath>(invalid_store_path(
                    utf8_path, "store path must not contain empty segments"));
            }
            const std::string segment = utf8_path.substr(segment_start, segment_end - segment_start);
            if (segment == "." || segment == "..")
            {
                return FileResult<StorePath>(invalid_store_path(
                    utf8_path, "store path must not contain dot segments"));
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
}
