#pragma once

#include "asset/game_project.h"
#include "file_system/native_platform_file.h"

#include <utility>

namespace toy3d
{
    // Immutable after opening; one editor process has one project association.
    class EditorProject
    {
      public:
        explicit EditorProject(PhysicalPath editor_directory) : editor_directory_(std::move(editor_directory))
        {
        }
        FileStatus open(const PhysicalPath& descriptor);
        static FileResult<PhysicalPath> create(const PhysicalPath& parent, const std::string& name,
                                               const PhysicalPath& editor_directory);
        bool active() const
        {
            return !descriptor_.empty();
        }
        const GameProject& description() const
        {
            return project_;
        }
        const PhysicalPath& descriptor() const
        {
            return descriptor_;
        }
        const PhysicalPath& root() const
        {
            return root_;
        }
        PhysicalPath assets() const
        {
            return PhysicalPath(root_.utf8() + "/asset");
        }
        PhysicalPath config() const
        {
            return PhysicalPath(root_.utf8() + "/config");
        }
        PhysicalPath shader() const
        {
            return PhysicalPath(root_.utf8() + "/shader");
        }
        PhysicalPath saved() const
        {
            return PhysicalPath(root_.utf8() + "/saved");
        }
        FileSystem& files()
        {
            return files_;
        }

      private:
        NativePlatformFile platform_;
        FileSystem files_;
        PhysicalPath descriptor_;
        PhysicalPath root_;
        PhysicalPath editor_directory_;
        GameProject project_;
    };
} // namespace toy3d
