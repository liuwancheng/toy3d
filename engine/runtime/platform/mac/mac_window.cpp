#include "mac_window.h"
#include "core/config/config_manager.h"
#include "core/input/input_system.h"
#include "mac_input.h"

namespace toy3d
{
    static void frame_buffer_size_cb(GLFWwindow* window, int width, int height)
    {
        MacWindow* win = reinterpret_cast<MacWindow*>(glfwGetWindowUserPointer(window));
        win->resize(width, height);
    }

    MacWindow::MacWindow(): IWindow()
    {
        // 初始化窗口
        create_glfw_window();

        // 初始化InputSystem
        platform_input_ = std::make_unique<MacPlatformInput>(glfw_window_);
        if (!platform_input_->init()) 
        {
            //LOG_ERROR("Failed to initialize platform input system");
            return ;
        }
    }

    MacWindow::~MacWindow()
    {
        destroy_glfw_window();
    }

    void MacWindow::create_glfw_window()
    {
        // 获取配置文件中的窗口标题和大小
        properties_.title = ConfigManager::get_instance().get_str("window_title", "toy3d");
        properties_.extent.width = ConfigManager::get_instance().get_int("window_width", 1280);
        properties_.extent.height = ConfigManager::get_instance().get_int("window_height", 720);
        properties_.vsync = static_cast<Vsync>(ConfigManager::get_instance().get_int("window_vsync", 0));
        properties_.mode = static_cast<Mode>(ConfigManager::get_instance().get_int("window_mode", 0));

        // glfw init
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfw_window_ = glfwCreateWindow(properties_.extent.width, properties_.extent.height, properties_.title.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(glfw_window_, this);
        glfwSetFramebufferSizeCallback(glfw_window_, frame_buffer_size_cb);

        glfwSetInputMode(glfw_window_, GLFW_STICKY_KEYS, 1);
        glfwSetInputMode(glfw_window_, GLFW_STICKY_MOUSE_BUTTONS, 1);
    }

	void MacWindow::destroy_glfw_window()
	{
        glfwDestroyWindow(glfw_window_);
        glfwTerminate();
	}

    void MacWindow::resize(uint32_t width, uint32_t height)
    {
        IWindow::resize(width, height);
    }

    bool MacWindow::should_close()
    {
        return glfwWindowShouldClose(glfw_window_);
    }

    void MacWindow::process_events()
    {
        glfwPollEvents();
    }

    void MacWindow::close()
    {
		glfwSetWindowShouldClose(glfw_window_, true);
    }
}