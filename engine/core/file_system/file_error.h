#pragma once

#include "file_system/physical_path.h"

#include <optional>
#include <string>
#include <utility>

namespace toy3d
{
    enum class FileErrorCode
    {
        None,
        NotFound,
        AlreadyExists,
        AccessDenied,
        ReadOnly,
        InvalidPath,
        InvalidData,
        InvalidState,
        OutsideRoot,
        OutsideMount = OutsideRoot,
        NotDirectory,
        IsDirectory,
        NotEmpty,
        CrossDevice,
        TooLarge,
        Busy,
        Unsupported,
        IoError
    };

    struct FileStatus
    {
        FileErrorCode code = FileErrorCode::None;
        std::string operation;
        PhysicalPath path;
        std::string virtual_path;
        std::string message;
        int platform_error = 0;

        bool succeeded() const;
        static FileStatus success();
    };

    template<typename T>
    class FileResult
    {
    public:
        explicit FileResult(T value)
            : value_(std::move(value))
        {
        }

        explicit FileResult(FileStatus status)
            : status_(std::move(status))
        {
        }

        bool succeeded() const
        {
            return value_.has_value() && status_.succeeded();
        }

        const FileStatus& status() const
        {
            return status_;
        }

        const T& value() const
        {
            return value_.value();
        }

        T& value()
        {
            return value_.value();
        }

    private:
        std::optional<T> value_;
        FileStatus status_;
    };
}
