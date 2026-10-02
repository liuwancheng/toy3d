#include "platform/platform_defines.h"
#include <utility>

#include <cstdio>
#include <string>
#include <vector>

#include "config/command_line_parser.h"

#if WITH_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace toy3d { int run_application_host(void* native_instance); }

#if WITH_WIN
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    // GUI launch does not allocate a second console; shell launch retains diagnostics.
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE* stream = nullptr;
        freopen_s(&stream, "conout$", "w", stdout);
        freopen_s(&stream, "conout$", "w", stderr);
    }
    int count = 0;
    wchar_t** native_arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!native_arguments) return 1;
    std::vector<std::string> arguments;
    for (int index = 0; index < count; ++index)
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native_arguments[index], -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) { LocalFree(native_arguments); return 1; }
        std::string value(static_cast<std::size_t>(bytes), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native_arguments[index], -1, value.data(), bytes, nullptr, nullptr))
        { LocalFree(native_arguments); return 1; }
        value.pop_back(); arguments.push_back(std::move(value));
    }
    LocalFree(native_arguments);
    toy3d::CommandLineParser::get_instance().parser_args(arguments);
    return toy3d::run_application_host(static_cast<void*>(instance));
}
#else
int main(int count, char** native_arguments)
{
    std::vector<std::string> arguments;
    for (int index = 0; index < count; ++index) arguments.emplace_back(native_arguments[index]);
    toy3d::CommandLineParser::get_instance().parser_args(arguments);
    return toy3d::run_application_host(nullptr);
}
#endif
