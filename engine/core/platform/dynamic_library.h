#pragma once

#include "file_system/file_error.h"
#include <string>

namespace toy3d
{
    // Owned OS image. Callers must destroy objects/callbacks defined in it before close.
    class DynamicLibrary final
    {
      public:
        DynamicLibrary() = default;
        ~DynamicLibrary();
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;
        bool open(const PhysicalPath& path, std::string& error);
        void* symbol(const char* name, std::string& error) const;
        void close();

      private:
        void* handle_ = nullptr;
    };

    FileResult<PhysicalPath> executable_file_path();
} // namespace toy3d
