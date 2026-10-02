#pragma once

#include "file_system/file_error.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    // OS-provided per-user application-data root; no cwd/environment fallback
    // on Windows. This queries a path and does not create directories.
    FileResult<PhysicalPath> user_data_directory();
    // Open an existing absolute directory in the user's desktop file manager.
    // Success confirms the request was accepted, not that the UI finished opening.
    // Failure returns a diagnostic for the caller; this function does not log.
    bool open_directory_on_desktop(const PhysicalPath& path, std::string& error);

    enum class ProcessError { None, InvalidArgument, Launch, Io, Wait, Timeout, Cancelled };

    struct ProcessRunOptions
    {
        std::uint32_t timeout_ms = 30000u;
        std::size_t maximum_output_bytes = 1024u * 1024u;
        const std::atomic<bool>* cancel = nullptr;
    };

    struct ProcessResult
    {
        bool launched = false;
        int exit_code = -1;
        std::string output;
        ProcessError error = ProcessError::None;
        std::string message;
        bool output_truncated = false;
        bool succeeded() const { return launched && error == ProcessError::None && exit_code == 0; }
    };

    // run owns and reaps its process tree; detached GUI processes belong to
    // the user. Implementations must pass arguments without shell evaluation.
    class ProcessService
    {
      public:
        virtual ~ProcessService() = default;
        virtual ProcessResult run(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                                  const ProcessRunOptions& options = {}) const = 0;
        virtual ProcessResult launch_detached(const PhysicalPath& executable,
                                              const std::vector<std::string>& arguments) const = 0;
    };

    class NativeProcessService final : public ProcessService
    {
      public:
        ProcessResult run(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                          const ProcessRunOptions& options = {}) const override;
        ProcessResult launch_detached(const PhysicalPath& executable,
                                      const std::vector<std::string>& arguments) const override;
    };
}
