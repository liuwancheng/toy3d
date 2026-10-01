#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "platform/platform_defines.h"

#if WITH_WIN
#include <Windows.h>
#endif

int run_helper(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) return 2;
    if (arguments[0] == "sleep") { std::this_thread::sleep_for(std::chrono::seconds(3)); return 0; }
#if WITH_WIN
    if (arguments[0] == "tree")
    {
        std::vector<wchar_t> executable(32768u, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length == executable.size()) return 2;
        std::wstring command = L"\"" + std::wstring(executable.data(), length) + L"\" sleep";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child)) return 2;
        std::cout << child.dwProcessId << '\n' << std::flush;
        if (!CloseHandle(child.hThread) || !CloseHandle(child.hProcess)) return 2;
        std::this_thread::sleep_for(std::chrono::seconds(3)); return 0;
    }
#endif
    if (arguments[0] == "flood") { for (int i = 0; i < 10000; ++i) std::cout << "0123456789abcdef"; return 0; }
    if (arguments[0] == "fail") { std::cerr << "failure"; return 7; }
    for (std::size_t i = 1; i < arguments.size(); ++i) std::cout << arguments[i].size() << ':' << arguments[i] << '\n';
    return 0;
}
#if WITH_WIN
int wmain(int count, wchar_t** values)
{
    std::vector<std::string> arguments;
    for (int i = 1; i < count; ++i)
    {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, values[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) return 2;
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, values[i], -1, text.data(), size, nullptr, nullptr)) return 2;
        text.pop_back(); arguments.push_back(std::move(text));
    }
    return run_helper(arguments);
}
#else
int main(int count, char** values)
{
    std::vector<std::string> arguments; for (int i = 1; i < count; ++i) arguments.emplace_back(values[i]); return run_helper(arguments);
}
#endif
