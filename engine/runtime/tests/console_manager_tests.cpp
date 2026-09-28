#include "config/command_line_parser.h"
#include "config/console_manager.h"

#include "file_system/directory_file_store.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // filesystem is used only to create and remove test configuration fixtures;
    // ConsoleManager itself remains independent of host path APIs.
    namespace fs = std::filesystem;

    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    struct TestDirectory
    {
        fs::path path;

        explicit TestDirectory(fs::path value) : path(std::move(value)) { fs::create_directories(path / "config"); }

        ~TestDirectory()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }
    };

    toy3d::VirtualPath virtual_path(const std::string& value)
    {
        auto parsed = toy3d::VirtualPath::parse(value);
        check(parsed.succeeded(), "test virtual path must parse: " + value);
        return parsed.succeeded() ? parsed.value() : toy3d::VirtualPath{};
    }
} // namespace

int main()
{
    const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    TestDirectory directory(fs::temp_directory_path() / ("toy3d_runtime_config_" + std::to_string(timestamp)));

    toy3d::NativePlatformFile platform_file;
    const toy3d::PhysicalPath config_path((directory.path / "config" / "engine_config.ini").u8string());
    const std::string config_text = "[Window]\r\n"
                                    "Width = 1280\r\n"
                                    "Height=720\r\n"
                                    "Title = Toy3d Test\r\n"
                                    "Fullscreen=false\r\n"
                                    "\r\n"
                                    "[Renderer]\n"
                                    "VSync=true # runtime default\n";
    check(platform_file.write_text_utf8(config_path, config_text, toy3d::FileWriteMode::CreateNew).succeeded(),
          "config fixture must be written through PlatformFile");

    toy3d::DirectoryFileStoreDesc store_desc;
    store_desc.physical_root = toy3d::PhysicalPath((directory.path / "config").u8string());
    store_desc.writable = false;
    store_desc.debug_name = "RuntimeConfigTest";
    auto store = toy3d::DirectoryFileStore::create(platform_file, store_desc);
    check(store.succeeded(), "config directory store must be created");
    if (!store.succeeded())
        return 1;

    toy3d::FileMountDesc mount;
    mount.virtual_root = virtual_path("/Engine/Config");
    mount.store = store.value();
    mount.access = toy3d::MountAccess::ReadOnly;
    mount.debug_name = "Engine";
    toy3d::FileSystem file_system;
    check(file_system.add_mount(mount).succeeded(), "engine config mount must register");
    mount.virtual_root = virtual_path("/Project/Config");
    check(file_system.add_mount(mount).succeeded(), "project config mount must register");
    check(file_system.freeze().succeeded(), "engine config mount must freeze");

    toy3d::ConsoleManager& console = toy3d::ConsoleManager::get_instance();
    console.reset_for_tests();
    check(console.load_config(file_system, virtual_path("/Engine/Config/engine_config.ini")).succeeded(),
          "ConsoleManager must load config through the virtual file system");
    check(console.get_int("Window.Width") == 1280, "section integer must parse");
    check(console.get_int("Window.Height") == 720, "CRLF input must parse");
    check(console.get_string("Window.Title") == "Toy3d Test", "trimmed text must parse");
    check(console.get_bool("Renderer.VSync", false), "inline comments must be removed");
    check(console.get_int("Missing", 42) == 42, "missing values must preserve defaults");

    const toy3d::PhysicalPath project_config_path((directory.path / "config" / "project.ini").u8string());
    check(platform_file.write_text_utf8(project_config_path, "[Window]\nWidth=1440\nTitle=Project Test\n",
                                       toy3d::FileWriteMode::CreateNew).succeeded(),
          "project config fixture must be written");
    check(console.load_config(file_system, virtual_path("/Project/Config/project.ini"),
                              toy3d::ConfigLoadMode::Overlay).succeeded(), "project config overlay must load");
    check(console.get_int("Window.Width") == 1440 && console.get_string("Window.Title") == "Project Test",
          "explicit project keys must override engine defaults");
    check(console.get_int("Window.Height") == 720 && console.get_bool("Renderer.VSync", false),
          "omitted project keys must retain engine defaults");
    check(console.load_config(file_system, virtual_path("/Project/Config/missing.ini"),
                              toy3d::ConfigLoadMode::Overlay).code == toy3d::FileErrorCode::NotFound &&
              console.get_int("Window.Width") == 1440 && console.get_int("Window.Height") == 720,
          "missing overlay must not discard valid engine or project settings");
    const toy3d::PhysicalPath invalid_config_path((directory.path / "config" / "invalid.ini").u8string());
    check(platform_file.write_binary(invalid_config_path, {0xffu}, toy3d::FileWriteMode::CreateNew).succeeded(),
          "invalid config fixture must be written");
    check(!console.load_config(file_system, virtual_path("/Project/Config/invalid.ini"),
                               toy3d::ConfigLoadMode::Overlay).succeeded() &&
              console.get_int("Window.Width") == 1440,
          "invalid UTF-8 overlay must preserve the last valid configuration");

    toy3d::CommandLineParser command_line;
    command_line.parser_args({"Toy3dEditor", "--resX=1600", "--resY=900", "--fullscreen", "--vsync=false"});
    command_line.apply_config();
    check(console.get_int("Window.Width") == 1600, "command line must override width");
    check(console.get_int("Window.Height") == 900, "command line must override height");
    check(console.get_bool("Window.Fullscreen", false), "fullscreen override must use config schema");
    check(!console.get_bool("Renderer.VSync", true), "vsync override must use config schema");

    const toy3d::FileStatus missing = console.load_config(file_system, virtual_path("/Engine/Config/missing.ini"));
    check(missing.code == toy3d::FileErrorCode::NotFound, "missing config must preserve the file-system diagnostic");
    check(console.get_int("Window.Width") == 1600, "failed reload must preserve the last valid configuration");

    console.reset_for_tests();
    check(console.get_int("Window.Width", 42) == 42, "test reset must clear global console state");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " console manager test(s) failed\n";
        return 1;
    }
    std::cout << "Toy3d console manager tests passed\n";
    return 0;
}
