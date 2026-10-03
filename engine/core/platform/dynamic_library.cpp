#include "platform/dynamic_library.h"

#include <filesystem>
#include <iostream>
#include <vector>
#include "platform/platform_defines.h"
#if WITH_WIN
#include <Windows.h>
#elif WITH_MAC || WITH_LINUX
#include <dlfcn.h>
#include <unistd.h>
#if WITH_MAC
#include <mach-o/dyld.h>
#endif
#endif

namespace toy3d
{
    // --------------------------------------------------------------------------
    // DynamicLibrary: explicit absolute-path native image ownership
    // --------------------------------------------------------------------------
    DynamicLibrary::~DynamicLibrary()
    {
        close();
    }
    bool DynamicLibrary::open(const PhysicalPath& path, std::string& error)
    {
        // filesystem preserves Unicode native paths and rejects implicit search paths.
        std::filesystem::path native;
        try
        {
            native = std::filesystem::u8path(path.utf8());
        }
        catch (const std::filesystem::filesystem_error& exception)
        {
            error = exception.what();
            return false;
        }
        if (handle_ || !native.is_absolute() || !path.valid())
        {
            error = "Dynamic library requires an absolute path and an empty owner.";
            return false;
        }
#if WITH_WIN
        handle_ = LoadLibraryExW(native.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle_)
        {
            error = "LoadLibrary failed (" + std::to_string(GetLastError()) + "): " + path.utf8();
        }
#elif WITH_MAC || WITH_LINUX
        handle_ = dlopen(native.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_)
        {
            error = dlerror();
        }
#else
        error = "Dynamic libraries are unsupported on this platform.";
#endif
        return handle_ != nullptr;
    }
    void* DynamicLibrary::symbol(const char* name, std::string& error) const
    {
        if (!handle_ || !name || !*name)
        {
            error = "Invalid dynamic library symbol request.";
            return nullptr;
        }
        void* result = nullptr;
#if WITH_WIN
        result = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#elif WITH_MAC || WITH_LINUX
        result = dlsym(handle_, name);
#endif
        if (!result)
        {
            error = std::string("Dynamic library symbol is missing: ") + name;
        }
        return result;
    }
    void DynamicLibrary::close()
    {
        if (!handle_)
        {
            return;
        }
#if WITH_WIN
        if (!FreeLibrary(static_cast<HMODULE>(handle_)))
        {
            std::cerr << "FreeLibrary failed (" << GetLastError() << ").\n";
        }
#elif WITH_MAC || WITH_LINUX
        if (dlclose(handle_) != 0)
        {
            std::cerr << "dlclose failed: " << dlerror() << '\n';
        }
#endif
        handle_ = nullptr;
    }
    FileResult<PhysicalPath> executable_file_path()
    {
        // filesystem converts the OS executable identity, independent of cwd.
#if WITH_WIN
        std::vector<wchar_t> text(32768u);
        const DWORD count = GetModuleFileNameW(nullptr, text.data(), static_cast<DWORD>(text.size()));
        if (count && count < text.size())
        {
            return FileResult<PhysicalPath>(
                PhysicalPath(std::filesystem::path(std::wstring(text.data(), count)).u8string()));
        }
#elif WITH_MAC
        std::uint32_t count = 0u;
        _NSGetExecutablePath(nullptr, &count);
        std::vector<char> text(count);
        if (_NSGetExecutablePath(text.data(), &count) == 0)
        {
            return FileResult<PhysicalPath>(PhysicalPath(std::filesystem::weakly_canonical(text.data()).u8string()));
        }
#elif WITH_LINUX
        std::vector<char> text(32768u);
        const auto count = readlink("/proc/self/exe", text.data(), text.size());
        if (count > 0 && static_cast<std::size_t>(count) < text.size())
        {
            return FileResult<PhysicalPath>(PhysicalPath(std::string(text.data(), static_cast<std::size_t>(count))));
        }
#endif
        return FileResult<PhysicalPath>(FileStatus{
            FileErrorCode::IoError, "executable_file_path", {}, {}, "Cannot resolve the current executable."});
    }
} // namespace toy3d
