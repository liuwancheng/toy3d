#include "file_system/file_error.h"

namespace toy3d
{
    bool FileStatus::succeeded() const
    {
        return code == FileErrorCode::None;
    }

    FileStatus FileStatus::success()
    {
        return {};
    }
} // namespace toy3d
