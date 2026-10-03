#include "platform/model_file_picker.h"
#include "platform/window_interface.h"

#import <Cocoa/Cocoa.h>

namespace toy3d
{
    namespace
    {
        enum class FileSelection
        {
            Model,
            Texture,
            Environment
        };
    bool pick_files(std::vector<std::string>& paths, std::string& error, FileSelection kind)
    {
        paths.clear();
        error.clear();
        @autoreleasepool
        {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.title = kind == FileSelection::Environment ? @"Import Environment" : kind == FileSelection::Texture ? @"Import Texture2D" : @"Import Static Mesh";
            panel.canChooseDirectories = NO;
            panel.canChooseFiles = YES;
            panel.allowsMultipleSelection = YES;
            panel.allowedFileTypes = kind == FileSelection::Environment ? @[@"hdr"] : kind == FileSelection::Texture ? @[@"png", @"jpg", @"jpeg"] : @[@"fbx", @"obj", @"gltf", @"glb"];
            if ([panel runModal] != NSModalResponseOK) return true;
            if (panel.URLs.count > maximum_file_drop_paths)
            { error = "Select at most 32 model files."; return false; }
            for (NSURL* url in panel.URLs)
            {
                const char* path = url.path.UTF8String;
                if (!path) { paths.clear(); error = "Model path conversion failed."; return false; }
                paths.emplace_back(path);
            }
        }
        return true;
    }
    }

    bool pick_model_files(IWindow&, std::vector<std::string>& paths, std::string& error)
    { return pick_files(paths, error, FileSelection::Model); }

    bool pick_environment_files(IWindow&, std::vector<std::string>& paths, std::string& error)
    {
        return pick_files(paths, error, FileSelection::Environment);
    }

    bool pick_texture_files(IWindow&, std::vector<std::string>& paths, std::string& error)
    { return pick_files(paths, error, FileSelection::Texture); }
    namespace
    {
        bool pick_project_path(std::string& path, std::string& error, bool folder)
        {
            path.clear(); error.clear();
            @autoreleasepool
            {
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                panel.title = folder ? @"New project parent folder" : @"Open Toy3d Project";
                panel.canChooseDirectories = folder;
                panel.canChooseFiles = !folder;
                panel.allowsMultipleSelection = NO;
                if (!folder) panel.allowedFileTypes = @[@"toy"];
                if ([panel runModal] != NSModalResponseOK) return true;
                const char* selected = panel.URL.path.UTF8String;
                if (!selected) { error = "Project path conversion failed."; return false; }
                path = selected;
            }
            return true;
        }
    }
    bool pick_project_file(IWindow&, std::string& path, std::string& error)
    { return pick_project_path(path, error, false); }
    bool pick_project_folder(IWindow&, std::string& path, std::string& error)
    { return pick_project_path(path, error, true); }

}
