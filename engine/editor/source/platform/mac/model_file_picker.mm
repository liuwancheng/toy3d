#include "platform/model_file_picker.h"
#include "platform/window_interface.h"

#import <Cocoa/Cocoa.h>

namespace toy3d
{
    namespace
    {
    bool pick_files(std::vector<std::string>& paths, std::string& error, bool texture)
    {
        paths.clear();
        error.clear();
        @autoreleasepool
        {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.title = texture ? @"Import Texture2D" : @"Import Static Mesh";
            panel.canChooseDirectories = NO;
            panel.canChooseFiles = YES;
            panel.allowsMultipleSelection = YES;
            panel.allowedFileTypes = texture ? @[@"png", @"jpg", @"jpeg"] : @[@"fbx", @"obj", @"gltf", @"glb"];
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
    { return pick_files(paths, error, false); }

    bool pick_texture_files(IWindow&, std::vector<std::string>& paths, std::string& error)
    { return pick_files(paths, error, true); }
}
