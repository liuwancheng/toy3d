#pragma once

#include "file_system/file_error.h"

#include <cstddef>
#include <cstdint>

namespace toy3d
{
    enum class FileOpenMode
    {
        Read,
        WriteNew,
        WriteTruncate,
        ReadWrite
    };

    class FileHandle
    {
    public:
        virtual ~FileHandle() = default;

        virtual FileResult<std::uint64_t> size() const = 0;
        virtual FileResult<std::size_t> read(std::uint8_t* destination, std::size_t byte_count) = 0;
        virtual FileResult<std::size_t> write(
            const std::uint8_t* source,
            std::size_t byte_count) = 0;
        virtual FileResult<std::uint64_t> tell() const = 0;
        virtual FileStatus seek(std::uint64_t offset) = 0;
        virtual FileResult<std::size_t> read_at(
            std::uint64_t offset,
            std::uint8_t* destination,
            std::size_t byte_count) const = 0;
        virtual FileStatus flush() = 0;
        virtual FileStatus close() = 0;
    };
}
