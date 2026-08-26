#pragma once

struct GLFWwindow;

namespace toy3d
{
    // AppKit view/layer mutation is completed while MacWindow is being created
    // on the main thread. The returned pointer is retained by the content view.
    void* attach_metal_layer(GLFWwindow* window);
}
