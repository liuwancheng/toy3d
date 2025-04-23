#include "android_window.h"
#include "core/config/config_manager.h"
#include "core/input/input_system.h"
#include "android_input.h"

namespace toy3d
{
    static void frame_buffer_size_cb(GLFWwindow* window, int width, int height)
    {
        AndroidWindow* win = reinterpret_cast<AndroidWindow*>(glfwGetWindowUserPointer(window));
        win->resize(width, height);
    }

    AndroidWindow::AndroidWindow(): IWindow()
    {
        // 初始化窗口
        create_glfw_window();

        // 初始化InputSystem
        platform_input_ = std::make_unique<AndroidPlatformInput>(glfw_window_);
        if (!platform_input_->init()) 
        {
            //LOG_ERROR("Failed to initialize platform input system");
            return ;
        }
    }

    AndroidWindow::~AndroidWindow()
    {
        destroy_glfw_window();
    }

    void AndroidWindow::create_glfw_window()
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

	void AndroidWindow::destroy_glfw_window()
	{
        glfwDestroyWindow(glfw_window_);
        glfwTerminate();
	}

    void AndroidWindow::resize(uint32_t width, uint32_t height)
    {
        IWindow::resize(width, height);
    }

    bool AndroidWindow::should_close()
    {
        return glfwWindowShouldClose(glfw_window_);
    }

    void AndroidWindow::process_events()
    {
        glfwPollEvents();
    }

    void AndroidWindow::close()
    {
		  glfwSetWindowShouldClose(glfw_window_, true);
    }
}