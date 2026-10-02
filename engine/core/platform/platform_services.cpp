#include "platform/platform_services.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <utility>
#include <cstdlib>

#include "misc/utf8.h"

#include "platform/platform_defines.h"

#if WITH_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace toy3d
{
    FileResult<PhysicalPath> user_data_directory()
    {
        FileStatus status;
        status.code = FileErrorCode::IoError;
        status.operation = "user_data_directory";
#if WITH_WIN
        PWSTR native = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &native);
        if (FAILED(result) || !native)
        {
            if (native)
            {
                CoTaskMemFree(native);
            }
            status.message = "Cannot resolve LocalAppData.";
            return FileResult<PhysicalPath>(status);
        }
        try
        {
            // filesystem converts the OS UTF-16 path without an ANSI codepage.
            const PhysicalPath path(std::filesystem::path(native).u8string());
            CoTaskMemFree(native);
            return FileResult<PhysicalPath>(path);
        }
        catch (const std::exception& exception)
        {
            CoTaskMemFree(native);
            status.message = exception.what();
            return FileResult<PhysicalPath>(status);
        }
#elif WITH_MAC || WITH_LINUX
#if WITH_LINUX
        const char* configured = std::getenv("XDG_DATA_HOME");
        if (configured && configured[0] == '/')
        {
            return FileResult<PhysicalPath>(PhysicalPath(configured));
        }
#endif
        const char* home = std::getenv("HOME");
        if (!home || home[0] != '/')
        {
            status.message = "Cannot resolve the absolute user home directory.";
            return FileResult<PhysicalPath>(status);
        }
#if WITH_MAC
        return FileResult<PhysicalPath>(PhysicalPath(std::string(home) + "/Library/Application Support"));
#else
        return FileResult<PhysicalPath>(PhysicalPath(std::string(home) + "/.local/share"));
#endif
#else
        status.code = FileErrorCode::Unsupported;
        status.message = "User application-data paths are unsupported on this platform.";
        return FileResult<PhysicalPath>(status);
#endif
    }

    namespace
    {
        constexpr std::size_t maximum_arguments = 256u;
        constexpr std::size_t maximum_argument_bytes = 32768u;
        constexpr std::size_t maximum_capture_bytes = 16u * 1024u * 1024u;
        constexpr std::uint32_t poll_interval_ms = 10u;
#if WITH_WIN
        constexpr DWORD cleanup_timeout_ms = 2000u;
#endif

        bool valid_text(const std::string& value)
        {
            return value.size() < maximum_argument_bytes && value.find('\0') == std::string::npos &&
                   is_valid_utf8(value);
        }
        bool validate(const PhysicalPath& executable, const std::vector<std::string>& arguments, ProcessResult& result)
        {
            if (executable.empty() || !valid_text(executable.utf8()) || arguments.size() > maximum_arguments)
            {
                result.error = ProcessError::InvalidArgument;
                result.message = "Invalid process executable or argument count.";
                return false;
            }
            for (const auto& argument : arguments)
            {
                if (!valid_text(argument))
                {
                    result.error = ProcessError::InvalidArgument;
                    result.message = "Invalid UTF-8, NUL or oversized process argument.";
                    return false;
                }
            }
            // filesystem checks native absolute-path syntax without resolving or
            // searching a different executable through PATH/current directory.
            if (!std::filesystem::u8path(executable.utf8()).is_absolute())
            {
                result.error = ProcessError::InvalidArgument;
                result.message = "Process executable must be an absolute path.";
                return false;
            }
            return true;
        }
        void append_output(ProcessResult& result, const char* data, std::size_t count, std::size_t limit)
        {
            const auto accepted = std::min(count, limit - result.output.size());
            result.output.append(data, accepted);
            result.output_truncated = result.output_truncated || accepted != count;
        }
        bool stop_requested(const ProcessRunOptions& options, const std::chrono::steady_clock::time_point& started,
                            ProcessResult& result)
        {
            if (options.cancel && options.cancel->load(std::memory_order_acquire))
            {
                result.error = ProcessError::Cancelled;
                result.message = "Process cancelled.";
                return true;
            }
            if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(options.timeout_ms))
            {
                result.error = ProcessError::Timeout;
                result.message = "Process timed out.";
                return true;
            }
            return false;
        }
#if WITH_WIN
        // --------------------------------------------------------------------------
        // NativeHandle: per-call ownership of Windows process and pipe handles
        // --------------------------------------------------------------------------
        class NativeHandle final
        {
          public:
            explicit NativeHandle(HANDLE value = nullptr) : value_(value)
            {
            }
            ~NativeHandle()
            {
                if (value_ && value_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(value_);
                }
            }
            NativeHandle(const NativeHandle&) = delete;
            NativeHandle& operator=(const NativeHandle&) = delete;
            HANDLE get() const
            {
                return value_;
            }
            bool close_now() noexcept
            {
                if (value_ && value_ != INVALID_HANDLE_VALUE && !CloseHandle(value_))
                {
                    return false;
                }
                value_ = nullptr;
                return true;
            }

          private:
            HANDLE value_ = nullptr;
        };
        std::wstring wide_text(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                 static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0)
            {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                    result.data(), size) != size)
            {
                return {};
            }
            return result;
        }
        std::wstring quote(const std::wstring& text)
        {
            // Always quote, including empty arguments; double trailing slashes
            // before the closing quote according to the Windows argv contract.
            std::wstring result = L"\"";
            std::size_t slashes = 0;
            for (const auto character : text)
            {
                if (character == L'\\')
                {
                    ++slashes;
                    continue;
                }
                result.append(slashes * (character == L'"' ? 2u : 1u), L'\\');
                slashes = 0;
                if (character == L'"')
                {
                    result.push_back(L'\\');
                }
                result.push_back(character);
            }
            result.append(slashes * 2u, L'\\');
            result.push_back(L'"');
            return result;
        }
        bool command(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                     std::wstring& application, std::wstring& line, ProcessResult& result)
        {
            application = wide_text(executable.utf8());
            line = quote(application);
            for (const auto& argument : arguments)
            {
                line.push_back(L' ');
                line += quote(wide_text(argument));
            }
            if (application.empty() || line.size() >= maximum_argument_bytes)
            {
                result.error = ProcessError::InvalidArgument;
                result.message = "Windows command line exceeds its limit.";
                return false;
            }
            return true;
        }
        void windows_error(ProcessResult& result, ProcessError error, const char* operation)
        {
            const DWORD code = GetLastError();
            if (result.error == ProcessError::None)
            {
                result.error = error;
            }
            if (!result.message.empty())
            {
                result.message += " ";
            }
            result.message += std::string(operation) + " failed (Win32 " + std::to_string(code) + ").";
        }
        void finish_windows_process(HANDLE process, NativeHandle& job, bool job_assigned, ProcessResult& result)
        {
            // This call owns the last Job handle. Close it before waiting so
            // kill-on-close stops the entire tree even on an error path.
            const bool job_closed = job.close_now();
            if (!job_closed)
            {
                windows_error(result, ProcessError::Wait, "Close process job");
            }
            if (!job_assigned || !job_closed)
            {
                // Assignment can fail while the newly created root is suspended.
                // Such a root is not covered by the Job's kill-on-close policy.
                const DWORD observed = WaitForSingleObject(process, 0u);
                if (observed != WAIT_OBJECT_0 && !TerminateProcess(process, 1u))
                {
                    windows_error(result, ProcessError::Wait, "Terminate uncontained process");
                }
            }
            const DWORD waited = WaitForSingleObject(process, cleanup_timeout_ms);
            if (waited == WAIT_TIMEOUT)
            {
                if (result.error == ProcessError::None)
                {
                    result.error = ProcessError::Wait;
                }
                if (!result.message.empty())
                {
                    result.message += " ";
                }
                result.message += "Process cleanup exceeded its " + std::to_string(cleanup_timeout_ms) +
                                  " ms deadline; exit was not confirmed.";
                return;
            }
            if (waited != WAIT_OBJECT_0)
            {
                windows_error(result, ProcessError::Wait, "Reap process");
                return;
            }
            DWORD code = 0;
            if (GetExitCodeProcess(process, &code))
            {
                result.exit_code = static_cast<int>(code);
            }
            else
            {
                windows_error(result, ProcessError::Wait, "GetExitCodeProcess");
            }
        }
#else
        // --------------------------------------------------------------------------
        // NativeFd: per-call ownership of POSIX pipe descriptors
        // --------------------------------------------------------------------------
        class NativeFd final
        {
          public:
            explicit NativeFd(int value = -1) : value_(value)
            {
            }
            ~NativeFd()
            {
                if (value_ >= 0)
                {
                    close(value_);
                }
            }
            NativeFd(const NativeFd&) = delete;
            NativeFd& operator=(const NativeFd&) = delete;
            int get() const
            {
                return value_;
            }
            int close_now()
            {
                const int value = value_;
                value_ = -1;
                return close(value);
            }

          private:
            int value_ = -1;
        };
        void posix_error(ProcessResult& result, ProcessError error, const char* operation, int code)
        {
            result.error = error;
            result.message = std::string(operation) + ": " + std::strerror(code);
        }
        std::vector<char*> make_argv(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                                     std::vector<std::string>& storage)
        {
            storage.push_back(executable.utf8());
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char*> argv;
            for (auto& item : storage)
            {
                argv.push_back(item.data());
            }
            argv.push_back(nullptr);
            return argv;
        }
#endif
    } // namespace

    // --------------------------------------------------------------------------
    // NativeProcessService: bounded execution and user-owned detached process launch
    // --------------------------------------------------------------------------
    ProcessResult NativeProcessService::run(const PhysicalPath& executable, const std::vector<std::string>& arguments,
                                            const ProcessRunOptions& options) const
    {
        ProcessResult result;
        if (!validate(executable, arguments, result))
        {
            return result;
        }
        if (!options.timeout_ms || options.maximum_output_bytes > maximum_capture_bytes)
        {
            result.error = ProcessError::InvalidArgument;
            result.message = "Invalid process timeout or output limit.";
            return result;
        }
        const auto started = std::chrono::steady_clock::now();
        if (stop_requested(options, started, result))
        {
            return result;
        }
        std::array<char, 4096u> buffer{};
#if WITH_WIN
        std::wstring application, line;
        if (!command(executable, arguments, application, line, result))
        {
            return result;
        }
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        HANDLE read_value = nullptr, write_value = nullptr;
        if (!CreatePipe(&read_value, &write_value, &security, 0))
        {
            windows_error(result, ProcessError::Io, "CreatePipe");
            return result;
        }
        NativeHandle read_handle(read_value);
        NativeHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                       OPEN_EXISTING, 0, nullptr));
        NativeHandle job(CreateJobObjectW(nullptr, nullptr));
        if (!SetHandleInformation(read_value, HANDLE_FLAG_INHERIT, 0) || input.get() == INVALID_HANDLE_VALUE ||
            !job.get())
        {
            windows_error(result, ProcessError::Io, "Prepare process handles");
            CloseHandle(write_value);
            return result;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            windows_error(result, ProcessError::Launch, "Configure process job");
            CloseHandle(write_value);
            return result;
        }
        // Explicit inheritance prevents concurrent compiler calls from keeping
        // another call's pipe open or leaking its process handles into children.
        SIZE_T attribute_size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
        std::vector<unsigned char> attribute_storage(attribute_size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size))
        {
            windows_error(result, ProcessError::Launch, "Initialize process attributes");
            CloseHandle(write_value);
            return result;
        }
        HANDLE inherited[] = {input.get(), write_value};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited),
                                       nullptr, nullptr))
        {
            windows_error(result, ProcessError::Launch, "Limit process handle inheritance");
            DeleteProcThreadAttributeList(attributes);
            CloseHandle(write_value);
            return result;
        }
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.get();
        startup.StartupInfo.hStdOutput = write_value;
        startup.StartupInfo.hStdError = write_value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION info{};
        const BOOL launched = CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                             nullptr, nullptr, &startup.StartupInfo, &info);
        if (!launched)
        {
            windows_error(result, ProcessError::Launch, "CreateProcessW");
        }
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(write_value);
        if (!launched)
        {
            return result;
        }
        NativeHandle process(info.hProcess), thread(info.hThread);
        const bool job_assigned = AssignProcessToJobObject(job.get(), process.get()) != FALSE;
        if (!job_assigned || ResumeThread(thread.get()) == static_cast<DWORD>(-1))
        {
            windows_error(result, ProcessError::Launch, "Assign/resume process job");
            finish_windows_process(process.get(), job, job_assigned, result);
            return result;
        }
        result.launched = true;
        bool exited = false;
        for (;;)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(read_handle.get(), nullptr, 0, nullptr, &available, nullptr))
            {
                if (GetLastError() != ERROR_BROKEN_PIPE)
                {
                    windows_error(result, ProcessError::Io, "PeekNamedPipe");
                }
                if (exited || result.error != ProcessError::None)
                {
                    break;
                }
            }
            else if (available)
            {
                DWORD count = 0;
                if (!ReadFile(read_handle.get(), buffer.data(), std::min(available, static_cast<DWORD>(buffer.size())),
                              &count, nullptr))
                {
                    windows_error(result, ProcessError::Io, "ReadFile");
                    break;
                }
                append_output(result, buffer.data(), count, options.maximum_output_bytes);
            }
            else if (exited)
            {
                break;
            }
            if (stop_requested(options, started, result))
            {
                break;
            }
            const DWORD waited = WaitForSingleObject(process.get(), available ? 0u : poll_interval_ms);
            if (waited == WAIT_FAILED)
            {
                windows_error(result, ProcessError::Wait, "WaitForSingleObject");
                break;
            }
            exited = waited == WAIT_OBJECT_0;
        }
        finish_windows_process(process.get(), job, job_assigned, result);
#else
        int descriptors[2]{};
        if (pipe(descriptors) != 0)
        {
            posix_error(result, ProcessError::Io, "pipe", errno);
            return result;
        }
        NativeFd read_handle(descriptors[0]), write_handle(descriptors[1]);
        if (fcntl(descriptors[0], F_SETFL, O_NONBLOCK) < 0 || fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) < 0)
        {
            posix_error(result, ProcessError::Io, "fcntl", errno);
            return result;
        }
        std::vector<std::string> storage;
        auto argv = make_argv(executable, arguments, storage);
        posix_spawn_file_actions_t actions{};
        posix_spawnattr_t attributes{};
        int code = posix_spawn_file_actions_init(&actions);
        if (code)
        {
            posix_error(result, ProcessError::Launch, "spawn actions", code);
            return result;
        }
        code = posix_spawnattr_init(&attributes);
        if (code)
        {
            posix_spawn_file_actions_destroy(&actions);
            posix_error(result, ProcessError::Launch, "spawn attributes", code);
            return result;
        }
        code = posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
        if (!code)
        {
            code = posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
        }
        if (!code)
        {
            code = posix_spawn_file_actions_addclose(&actions, descriptors[0]);
        }
        if (!code)
        {
            code = posix_spawn_file_actions_addclose(&actions, descriptors[1]);
        }
        if (!code)
        {
            code = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        }
        if (!code)
        {
            code = posix_spawnattr_setpgroup(&attributes, 0);
        }
        pid_t child = -1;
        if (!code)
        {
            code = posix_spawn(&child, executable.utf8().c_str(), &actions, &attributes, argv.data(), environ);
        }
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (code)
        {
            posix_error(result, ProcessError::Launch, "posix_spawn", code);
            return result;
        }
        if (write_handle.close_now() < 0)
        {
            posix_error(result, ProcessError::Io, "close parent pipe writer", errno);
        }
        result.launched = true;
        int status = 0;
        bool exited = false;
        while (result.error == ProcessError::None)
        {
            const ssize_t count = read(read_handle.get(), buffer.data(), buffer.size());
            if (count > 0)
            {
                append_output(result, buffer.data(), static_cast<std::size_t>(count), options.maximum_output_bytes);
            }
            else if (count < 0 && errno != EAGAIN && errno != EINTR)
            {
                posix_error(result, ProcessError::Io, "read", errno);
                break;
            }
            else if (exited)
            {
                break;
            }
            if (stop_requested(options, started, result))
            {
                break;
            }
            if (!exited)
            {
                // Observe without reaping: the PID still belongs to this call
                // until its process group is stopped, avoiding PID reuse races.
                siginfo_t information{};
                const int waited = waitid(P_PID, static_cast<id_t>(child), &information, WEXITED | WNOHANG | WNOWAIT);
                if (waited < 0 && errno != EINTR)
                {
                    posix_error(result, ProcessError::Wait, "waitid", errno);
                    break;
                }
                exited = waited == 0 && information.si_pid == child;
            }
            if (count <= 0)
            {
                pollfd descriptor{read_handle.get(), POLLIN, 0};
                if (poll(&descriptor, 1, poll_interval_ms) < 0 && errno != EINTR)
                {
                    posix_error(result, ProcessError::Io, "poll", errno);
                    break;
                }
            }
        }
        if (kill(-child, SIGKILL) < 0 && errno != ESRCH)
        {
            posix_error(result, ProcessError::Wait, "kill process group", errno);
        }
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0)
        {
            posix_error(result, ProcessError::Wait, "reap process", errno);
        }
        if (WIFEXITED(status))
        {
            result.exit_code = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            result.exit_code = 128 + WTERMSIG(status);
        }
#endif
        return result;
    }

    ProcessResult NativeProcessService::launch_detached(const PhysicalPath& executable,
                                                        const std::vector<std::string>& arguments) const
    {
        ProcessResult result;
        if (!validate(executable, arguments, result))
        {
            return result;
        }
#if WITH_WIN
        std::wstring application, line;
        if (!command(executable, arguments, application, line, result))
        {
            return result;
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            nullptr, &startup, &info))
        {
            windows_error(result, ProcessError::Launch, "Launch detached process");
            return result;
        }
        NativeHandle process(info.hProcess), thread(info.hThread);
        result.launched = true;
        result.exit_code = 0;
#else
        // Prebuild storage before fork: children only use async-signal-safe
        // syscalls, and exec failure is acknowledged before returning success.
        std::vector<std::string> storage;
        auto argv = make_argv(executable, arguments, storage);
        int descriptors[2]{};
        if (pipe(descriptors) < 0)
        {
            posix_error(result, ProcessError::Io, "pipe", errno);
            return result;
        }
        NativeFd read_handle(descriptors[0]), write_handle(descriptors[1]);
        if (fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) < 0)
        {
            posix_error(result, ProcessError::Io, "fcntl", errno);
            return result;
        }
        const pid_t child = fork();
        if (child < 0)
        {
            posix_error(result, ProcessError::Launch, "fork", errno);
            return result;
        }
        if (child == 0)
        {
            close(descriptors[0]);
            if (setsid() >= 0)
            {
                const pid_t grandchild = fork();
                if (grandchild > 0)
                {
                    _exit(0);
                }
                if (grandchild == 0)
                {
                    execv(argv[0], argv.data());
                }
            }
            const int error = errno;
            const auto ignored = write(descriptors[1], &error, sizeof(error));
            (void)ignored;
            _exit(1);
        }
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0)
        {
            posix_error(result, ProcessError::Wait, "waitpid", errno);
            return result;
        }
        // Close our write end before waiting for the child's exec acknowledgement.
        if (write_handle.close_now() < 0)
        {
            posix_error(result, ProcessError::Io, "close pipe", errno);
            return result;
        }
        int error = 0;
        ssize_t count;
        do
        {
            count = read(descriptors[0], &error, sizeof(error));
        } while (count < 0 && errno == EINTR);
        if (count != 0)
        {
            posix_error(result, ProcessError::Launch, "execv", count < 0 ? errno : error);
            return result;
        }
        result.launched = true;
        result.exit_code = 0;
#endif
        return result;
    }

    bool open_directory_on_desktop(const PhysicalPath& path, std::string& error)
    {
        error.clear();
        if (!path.valid() || path.empty())
        {
            error = "The directory path is invalid.";
            return false;
        }
        // C++17 filesystem preserves native UTF-16 for ShellExecute and checks
        // that this desktop action opens an existing absolute directory.
        const auto native = std::filesystem::u8path(path.utf8());
        std::error_code status;
        if (!native.is_absolute() || !std::filesystem::is_directory(native, status))
        {
            error = "Cannot open directory: " + path.utf8();
            if (status)
            {
                error += " (" + status.message() + ")";
            }
            return false;
        }
#if WITH_WIN
        const auto result =
            reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32)
        {
            error = "Unable to open directory (code " + std::to_string(result) + ").";
            return false;
        }
        return true;
#elif WITH_MAC
        const auto result = NativeProcessService{}.launch_detached(PhysicalPath("/usr/bin/open"), {path.utf8()});
        if (!result.succeeded())
        {
            error = result.message;
            return false;
        }
        return true;
#else
        error = "Opening a desktop directory is unsupported on this platform.";
        return false;
#endif
    }
} // namespace toy3d
