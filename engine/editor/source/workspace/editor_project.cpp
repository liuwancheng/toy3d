#include "workspace/editor_project.h"

#include <filesystem>

#include "file_system/directory_file_store.h"
#include "config/console_manager.h"

namespace toy3d
{
    namespace
    {
        constexpr const char* launch_batch = R"TOY(@echo off
rem Toy3d project Editor launcher
setlocal EnableExtensions DisableDelayedExpansion
chcp 65001 >nul
set "TOY3D_DESCRIPTOR="
set "TOY3D_DEFAULT_BIN="
if exist "%~dp0saved\editor_launch.txt" (
    rem FOR /F handles the UTF-8 LF record; SET /P requires CRLF.
    for /f "usebackq eol=| delims=" %%L in ("%~dp0saved\editor_launch.txt") do (
        if not defined TOY3D_DESCRIPTOR (
            set "TOY3D_DESCRIPTOR=%%L"
        ) else (
            set "TOY3D_DEFAULT_BIN=%%L"
        )
    )
)
if defined TOY3D_DESCRIPTOR if not exist "%~dp0%TOY3D_DESCRIPTOR%" set "TOY3D_DESCRIPTOR="
if defined TOY3D_DESCRIPTOR goto descriptor_ready
for %%F in ("%~dp0*.toy") do if exist "%%~fF" (
    if defined TOY3D_DESCRIPTOR goto ambiguous_project
    set "TOY3D_DESCRIPTOR=%%~nxF"
)
if not defined TOY3D_DESCRIPTOR goto missing_project
:descriptor_ready
if defined TOY3D_EDITOR_BIN (
    set "TOY3D_BIN=%TOY3D_EDITOR_BIN%"
) else if defined TOY3D_DEFAULT_BIN (
    set "TOY3D_BIN=%TOY3D_DEFAULT_BIN%"
) else (
    set "TOY3D_BIN=%~dp0..\bin"
)
if not exist "%TOY3D_BIN%\Toy3dEditor.exe" goto missing_editor
"%TOY3D_BIN%\Toy3dEditor.exe" %* "--Project=%~dp0%TOY3D_DESCRIPTOR%"
set "TOY3D_LAUNCH_RESULT=%errorlevel%"
if "%TOY3D_LAUNCH_RESULT%"=="0" exit /b 0
if "%~1"=="" pause
exit /b %TOY3D_LAUNCH_RESULT%
:ambiguous_project
echo ERROR: Multiple .toy files. Open the intended project in Editor to refresh saved/editor_launch.txt. 1>&2
goto failed
:missing_project
echo ERROR: No .toy project beside this launcher. 1>&2
goto failed
:missing_editor
echo ERROR: Editor was not found. Build the engine or set TOY3D_EDITOR_BIN to its bin directory. 1>&2
:failed
if "%~1"=="" pause
exit /b 1
)TOY";

        constexpr const char* launch_shell = R"TOY(#!/bin/sh
# Toy3d project Editor launcher
set -eu
case "$0" in /*) script="$0" ;; *) script="./$0" ;; esac
project_dir=$(CDPATH= cd -P "$(dirname "$script")" && pwd -P)
descriptor=
default_bin=
if [ -f "$project_dir/saved/editor_launch.txt" ]; then
    { IFS= read -r descriptor || :; IFS= read -r default_bin || :; } < "$project_dir/saved/editor_launch.txt"
fi
if [ -n "$descriptor" ] && [ ! -f "$project_dir/$descriptor" ]; then descriptor=; fi
if [ -z "$descriptor" ]; then
    for file in "$project_dir"/*.toy; do
        [ -f "$file" ] || continue
        if [ -n "$descriptor" ]; then
            echo "ERROR: Multiple .toy files. Open the intended project in Editor to refresh saved/editor_launch.txt." >&2
            exit 1
        fi
        descriptor=${file##*/}
    done
fi
if [ -z "$descriptor" ]; then echo "ERROR: No .toy project beside this launcher." >&2; exit 1; fi
editor_bin=${TOY3D_EDITOR_BIN:-${default_bin:-"$project_dir/../bin"}}
editor="$editor_bin/Toy3dEditor"
if [ -x "$editor_bin/Toy3dEditor.app/Contents/MacOS/Toy3dEditor" ]; then
    editor="$editor_bin/Toy3dEditor.app/Contents/MacOS/Toy3dEditor"
fi
if [ ! -x "$editor" ]; then
    echo "ERROR: Editor was not found. Build the engine or set TOY3D_EDITOR_BIN to its bin directory." >&2
    exit 1
fi
exec "$editor" "$@" "--Project=$project_dir/$descriptor"
)TOY";

        bool valid_editor_directory(const PhysicalPath& directory)
        {
            // C++17 filesystem checks native absolute paths without CWD fallback.
            return directory.valid() && !directory.empty() && directory.utf8().find_first_of("\r\n") == std::string::npos &&
                std::filesystem::u8path(directory.utf8()).is_absolute();
        }

        FileStatus mount_project(FileSystem& files, NativePlatformFile& platform, const PhysicalPath& root)
        {
            DirectoryFileStoreDesc desc; desc.physical_root = root; desc.writable = true; desc.debug_name = "GameProject";
            const auto store = DirectoryFileStore::create(platform, desc);
            if (!store.succeeded()) return store.status();
            FileMountDesc mount; mount.virtual_root = VirtualPath::parse("/Game").value();
            mount.store = store.value(); mount.access = MountAccess::ReadWrite; mount.allow_enumeration = true;
            const auto added = files.add_mount(mount);
            return added.succeeded() ? files.freeze() : added;
        }
        FileStatus project_error(const std::string& message)
        { return {FileErrorCode::InvalidData, "editor_project", {}, {}, message}; }
        FileStatus write_text(FileSystem& files, const char* path, const std::string& text)
        {
            return files.write_binary_atomic(VirtualPath::parse(path).value(),
                std::vector<std::uint8_t>(text.begin(), text.end()), FilePublishMode::CreateNew);
        }
        FileStatus ensure_project_file(FileSystem& files, const char* filename, const std::string& text)
        {
            const auto path = VirtualPath::parse(std::string("/Game/") + filename).value();
            auto existing = files.stat(path);
            if (!existing.succeeded() && existing.status().code == FileErrorCode::NotFound)
            {
                const auto written = write_text(files, path.utf8().c_str(), text);
                if (written.succeeded() || written.code != FileErrorCode::AlreadyExists) return written;
                // Another Editor may fill the same missing file first.
                existing = files.stat(path);
            }
            if (!existing.succeeded()) return existing.status();
            return existing.value().type == FileType::File ? FileStatus::success() :
                project_error("Generated project file must be a regular file: " + path.utf8());
        }
        FileStatus write_launchers(FileSystem& files, const std::string& filename, const PhysicalPath& editor_directory)
        {
            auto status = ensure_project_file(files, "launch_editor.bat", launch_batch);
            if (status.succeeded()) status = ensure_project_file(files, "launch_editor.sh", launch_shell);
            if (status.succeeded()) status = ensure_project_file(files, ".gitignore", "/saved/\n");
            if (status.succeeded()) status = ensure_project_file(files, ".gitattributes", "launch_editor.sh text eol=lf\n");
            if (!status.succeeded()) return status;
            // Saved stores local installation data; source-side scripts stay portable
            // and existing custom launchers are never replaced.
            const std::string metadata = filename + "\n" + editor_directory.utf8() + "\n";
            return files.write_binary_atomic(VirtualPath::parse("/Game/saved/editor_launch.txt").value(),
                std::vector<std::uint8_t>(metadata.begin(), metadata.end()), FilePublishMode::Replace);
        }
    }

    FileStatus EditorProject::open(const PhysicalPath& descriptor)
    {
        if (active() || files_.frozen()) return project_error("Project association cannot change within this instance.");
        if (!valid_editor_directory(editor_directory_)) return project_error("Editor deployment directory must be an absolute single-line path.");
        const auto canonical = platform_.canonical(descriptor);
        if (!canonical.succeeded()) return canonical.status();
        const auto parent = platform_.parent_path(canonical.value());
        if (!parent.succeeded()) return parent.status();
        const std::string filename = canonical.value().utf8().substr(canonical.value().utf8().find_last_of("/\\") + 1u);
        if (filename.size() <= 4u || filename.compare(filename.size() - 4u, 4u, ".toy") != 0)
            return project_error("Choose a .toy project descriptor.");
        const auto mounted = mount_project(files_, platform_, parent.value());
        if (!mounted.succeeded()) return mounted;
        const auto loaded = read_game_project(files_, VirtualPath::parse("/Game/" + filename).value());
        if (!loaded.succeeded()) return loaded.status();
        if (loaded.value().engine_association != "toy3d_dev")
            return project_error("This Editor belongs to engine association toy3d_dev.");
        if (!loaded.value().modules.empty())
            return project_error("C++ game modules require a project Editor host; this version supports resource projects.");
        // Optional content directories are created through the rooted store;
        // reparse points cannot redirect authoring outside the project.
        for (const char* name : {"asset", "config", "shader/include", "saved"})
        {
            const auto made = files_.create_directories(VirtualPath::parse(std::string("/Game/") + name).value());
            if (!made.succeeded()) return made;
        }
        const auto config_path = VirtualPath::parse("/Game/config/game_engine.ini").value();
        const auto config = files_.read_text_utf8(config_path, 256u * 1024u);
        if (!config.succeeded() && config.status().code != FileErrorCode::NotFound) return config.status();
        if (config.succeeded())
        {
            const auto checked = ConsoleManager::parse_config(config.value(), config_path.utf8());
            if (!checked.succeeded()) return checked.status();
        }
        const auto launchers = write_launchers(files_, filename, editor_directory_);
        if (!launchers.succeeded()) return launchers;
        project_ = loaded.value(); root_ = parent.value(); descriptor_ = canonical.value();
        return FileStatus::success();
    }

    FileResult<PhysicalPath> EditorProject::create(const PhysicalPath& parent, const std::string& name,
                                                   const PhysicalPath& editor_directory)
    {
        if (!valid_editor_directory(editor_directory)) return FileResult<PhysicalPath>(project_error("Editor deployment directory must be an absolute single-line path."));
        if (!valid_project_name(name)) return FileResult<PhysicalPath>(project_error("Use a project name beginning with a letter, then letters, digits or underscore (64 maximum)."));
        NativePlatformFile platform;
        const auto root = platform.canonical(parent);
        if (!root.succeeded()) return FileResult<PhysicalPath>(root.status());
        GameProject project; project.name = name; project.engine_association = "toy3d_dev";
        if (!AssetId::try_generate(project.id)) return FileResult<PhysicalPath>(project_error("Could not generate project identity."));
        const auto text = encode_game_project(project);
        if (!text.succeeded()) return FileResult<PhysicalPath>(text.status());
        const auto staging = platform.join_relative(root.value(), ".toy3d-create-" + project.id.hex());
        const auto target = platform.join_relative(root.value(), name);
        if (!staging.succeeded() || !target.succeeded()) return FileResult<PhysicalPath>(project_error("Invalid project destination."));
        const auto exists = platform.exists(target.value());
        if (!exists.succeeded()) return FileResult<PhysicalPath>(exists.status());
        if (exists.value()) return FileResult<PhysicalPath>(project_error("Destination already exists; choose a new folder name."));
        const auto created = platform.create_directory(staging.value());
        if (!created.succeeded()) return FileResult<PhysicalPath>(created);
        FileSystem files;
        auto status = mount_project(files, platform, staging.value());
        for (const char* folder : {"asset", "config", "shader/include", "src", "saved"})
            if (status.succeeded()) status = files.create_directories(VirtualPath::parse(std::string("/Game/") + folder).value());
        if (status.succeeded()) status = write_text(files, "/Game/config/game_engine.ini",
            "; Empty startup scenes inherit the engine default.\n[Editor]\nStartupScene=\n\n[Game]\nStartupScene=\n");
        if (status.succeeded()) status = write_launchers(files, name + ".toy", editor_directory);
        if (status.succeeded()) status = write_text(files, ("/Game/" + name + ".toy").c_str(), text.value());
        if (status.succeeded()) status = platform.rename_no_replace(staging.value(), target.value());
        if (!status.succeeded())
        {
            // Only this unique, successfully-created staging tree is owned here.
            const auto removed = platform.remove_directory_tree(staging.value());
            if (!removed.succeeded()) status.message += " Staging retained: " + staging.value().utf8() + ": " + removed.status().message;
            return FileResult<PhysicalPath>(std::move(status));
        }
        return platform.join_relative(target.value(), name + ".toy");
    }
}
