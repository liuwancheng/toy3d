#include "mac_window.h"
#include "config/console_manager.h"
#include "input/input_system.h"
#include "logging/logger.h"
#include "mac_input.h"
#include "mac_metal_layer.h"

namespace toy3d
{
    static void frame_buffer_size_cb(GLFWwindow* window, int width, int height)
    {
        MacWindow* win = reinterpret_cast<MacWindow*>(glfwGetWindowUserPointer(window));
        win->resize(width, height);
    }

    MacWindow::MacWindow() : IWindow()
    {
        // 初始化窗口
        if (!create_glfw_window())
        {
            return;
        }

        // 初始化InputSystem
        platform_input = std::make_unique<MacPlatformInput>(glfw_window);
        if (!platform_input->init())
        {
            // LOG_ERROR("Failed to initialize platform input system");
            return;
        }
    }

    MacWindow::~MacWindow()
    {
        destroy_glfw_window();
    }

    bool MacWindow::create_glfw_window()
    {
        // 获取配置文件中的窗口标题和大小
        const ConsoleManager& console = ConsoleManager::get_instance();
        properties.title = console.get_string("Window.Title", "toy3d");
        const int configured_width = console.get_int("Window.Width", 1280);
        const int configured_height = console.get_int("Window.Height", 720);
        properties.extent.width = configured_width > 0 ? static_cast<std::uint32_t>(configured_width) : 1U;
        properties.extent.height = configured_height > 0 ? static_cast<std::uint32_t>(configured_height) : 1U;
        properties.vsync = console.get_bool("Renderer.VSync", true) ? Vsync::ON : Vsync::OFF;
        properties.mode = console.get_bool("Window.Fullscreen", false) ? Mode::Fullscreen : Mode::Default;

        // glfw init
        if (glfwInit() != GLFW_TRUE)
        {
            const char* error = nullptr;
            glfwGetError(&error);
            TOY_LOG_ERROR("Failed to initialize GLFW on macOS: {}", error ? error : "unknown error");
            return false;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfw_window = glfwCreateWindow(properties.extent.width, properties.extent.height, properties.title.c_str(),
                                       nullptr, nullptr);
        if (glfw_window == nullptr)
        {
            const char* error = nullptr;
            glfwGetError(&error);
            TOY_LOG_ERROR("Failed to create the macOS GLFW window: {}", error ? error : "unknown error");
            glfwTerminate();
            return false;
        }
        metal_layer = attach_metal_layer(glfw_window);
        if (metal_layer == nullptr)
        {
            TOY_LOG_ERROR("Failed to attach the macOS Metal presentation layer on the main thread.");
            glfwDestroyWindow(glfw_window);
            glfw_window = nullptr;
            glfwTerminate();
            return false;
        }
        glfwSetWindowUserPointer(glfw_window, this);
        glfwSetFramebufferSizeCallback(glfw_window, frame_buffer_size_cb);

        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(glfw_window, &framebuffer_width, &framebuffer_height);
        resize(static_cast<uint32_t>(framebuffer_width), static_cast<uint32_t>(framebuffer_height));

        glfwSetInputMode(glfw_window, GLFW_STICKY_KEYS, 1);
        glfwSetInputMode(glfw_window, GLFW_STICKY_MOUSE_BUTTONS, 1);
        return true;
    }

    void MacWindow::destroy_glfw_window()
    {
        if (glfw_window != nullptr)
        {
            metal_layer = nullptr;
            glfwDestroyWindow(glfw_window);
            glfw_window = nullptr;
        }
        glfwTerminate();
    }

    void MacWindow::resize(uint32_t _width, uint32_t _height)
    {
        IWindow::resize(_width, _height);
    }

    Extent MacWindow::get_display_size() const
    {
        int width = 0;
        int height = 0;
        if (glfw_window != nullptr)
        {
            glfwGetWindowSize(glfw_window, &width, &height);
        }
        return {width > 0 ? static_cast<std::uint32_t>(width) : 0u,
                height > 0 ? static_cast<std::uint32_t>(height) : 0u};
    }

    Extent MacWindow::get_framebuffer_size() const
    {
        int width = 0;
        int height = 0;
        if (glfw_window != nullptr)
        {
            glfwGetFramebufferSize(glfw_window, &width, &height);
        }
        return {width > 0 ? static_cast<std::uint32_t>(width) : 0u,
                height > 0 ? static_cast<std::uint32_t>(height) : 0u};
    }

    bool MacWindow::should_close()
    {
        return glfw_window == nullptr || glfwWindowShouldClose(glfw_window);
    }

    void MacWindow::process_events()
    {
        glfwPollEvents();
    }

    void MacWindow::close()
    {
        if (glfw_window != nullptr)
        {
            glfwSetWindowShouldClose(glfw_window, true);
        }
    }
} // namespace toy3d
