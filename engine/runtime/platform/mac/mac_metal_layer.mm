#include "platform/mac/mac_metal_layer.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace toy3d
{
    void* attach_metal_layer(GLFWwindow* window)
    {
        @autoreleasepool
        {
            if (window == nullptr || ![NSThread isMainThread])
            {
                return nullptr;
            }

            NSWindow* const native_window = glfwGetCocoaWindow(window);
            NSView* const content_view = [native_window contentView];
            if (native_window == nil || content_view == nil)
            {
                return nullptr;
            }

            CAMetalLayer* const layer = [CAMetalLayer layer];
            if (layer == nil)
            {
                return nullptr;
            }
            [layer setContentsScale:[native_window backingScaleFactor]];
            [content_view setLayer:layer];
            [content_view setWantsLayer:YES];
            return reinterpret_cast<void*>(layer);
        }
    }
}
