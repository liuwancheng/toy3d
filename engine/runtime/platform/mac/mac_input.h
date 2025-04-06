#pragma once
#include "platform/platform_input_interface.h"

#include <GLFW/glfw3.h>

namespace toy3d
{
    class MacPlatformInput : public IPlatformInput 
    {
    public:
        MacPlatformInput(GLFWwindow* window) :IPlatformInput(), glfw_window_(window) {}
        virtual ~MacPlatformInput(){};
    public:        
        bool init() override;
        void exit() override;
        void update() override;
    private:
        GLFWwindow* glfw_window_ = nullptr;
        
        // GLFW回调函数
        static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void mousebutton_callback(GLFWwindow* window, int button, int action, int mods);
        static void mousemove_callback(GLFWwindow* window, double xpos, double ypos);
        static void mousewheel_callback(GLFWwindow* window, double xoffset, double yoffset);
    };
} // namespace toy3d