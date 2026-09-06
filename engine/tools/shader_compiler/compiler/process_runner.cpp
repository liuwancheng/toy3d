#include "compiler/process_runner.h"

#include <array>
#include <filesystem>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace toy3d::shader
{
    namespace
    {
#if defined(_WIN32)
        std::wstring utf8_to_wide(const std::string& value)
        {
            if (value.empty())
                return {};
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                  static_cast<int>(value.size()), nullptr, 0);
            if (count <= 0)
                return {};
            std::wstring result(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), count) != count)
            {
                return {};
            }
            return result;
        }

        std::wstring quote_argument(const std::wstring& argument)
        {
            if (argument.find_first_of(L" \t\"") == std::wstring::npos)
                return argument;
            std::wstring result = L"\"";
            std::size_t backslashes = 0;
            for (const wchar_t character : argument)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                }
                else if (character == L'\"')
                {
                    result.append(backslashes * 2u + 1u, L'\\');
                    result.push_back(L'\"');
                    backslashes = 0;
                }
                else
                {
                    result.append(backslashes, L'\\');
                    backslashes = 0;
                    result.push_back(character);
                }
            }
            result.append(backslashes * 2u, L'\\');
            result.push_back(L'\"');
            return result;
        }
#endif
    } // namespace

    ProcessResult run_process(const PhysicalPath& executable, const std::vector<std::string>& arguments)
    {
        ProcessResult result;
#if defined(_WIN32)
        // filesystem performs the UTF-8 to native Windows path conversion used
        // by CreateProcessW without a second custom conversion path.
        const std::wstring application = std::filesystem::u8path(executable.utf8()).wstring();
        if (application.empty())
            return result;
        std::wstring command_line = quote_argument(application);
        for (const std::string& argument : arguments)
        {
            const std::wstring wide = utf8_to_wide(argument);
            if (!argument.empty() && wide.empty())
                return result;
            command_line.push_back(L' ');
            command_line += quote_argument(wide);
        }

        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        HANDLE read_handle = nullptr;
        HANDLE write_handle = nullptr;
        if (!CreatePipe(&read_handle, &write_handle, &attributes, 0))
            return result;
        if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0))
        {
            CloseHandle(read_handle);
            CloseHandle(write_handle);
            return result;
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write_handle;
        startup.hStdError = write_handle;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        result.launched = CreateProcessW(application.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
        CloseHandle(write_handle);
        if (!result.launched)
        {
            CloseHandle(read_handle);
            return result;
        }
        std::array<char, 4096> buffer{};
        DWORD read_count = 0;
        while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read_count, nullptr) &&
               read_count != 0)
        {
            result.output.append(buffer.data(), read_count);
        }
        CloseHandle(read_handle);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process.hProcess, &exit_code))
            result.exit_code = static_cast<int>(exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        int pipe_handles[2]{};
        if (pipe(pipe_handles) != 0)
            return result;
        const pid_t process = fork();
        if (process < 0)
        {
            close(pipe_handles[0]);
            close(pipe_handles[1]);
            return result;
        }
        if (process == 0)
        {
            dup2(pipe_handles[1], STDOUT_FILENO);
            dup2(pipe_handles[1], STDERR_FILENO);
            close(pipe_handles[0]);
            close(pipe_handles[1]);
            std::vector<std::string> storage;
            storage.reserve(arguments.size() + 1u);
            storage.push_back(executable.utf8());
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char*> argv;
            argv.reserve(storage.size() + 1u);
            for (std::string& item : storage)
                argv.push_back(item.data());
            argv.push_back(nullptr);
            execv(argv.front(), argv.data());
            _exit(errno == ENOENT ? 127 : 126);
        }
        result.launched = true;
        close(pipe_handles[1]);
        std::array<char, 4096> buffer{};
        ssize_t read_count = 0;
        while ((read_count = read(pipe_handles[0], buffer.data(), buffer.size())) > 0)
        {
            result.output.append(buffer.data(), static_cast<std::size_t>(read_count));
        }
        close(pipe_handles[0]);
        int status = 0;
        if (waitpid(process, &status, 0) >= 0)
        {
            if (WIFEXITED(status))
                result.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                result.exit_code = 128 + WTERMSIG(status);
        }
#endif
        return result;
    }
} // namespace toy3d::shader
