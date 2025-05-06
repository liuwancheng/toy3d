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
        platform_input = std::make_unique<AndroidPlatformInput>(glfw_window);
        if (!platform_input->init()) 
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
        properties.title = ConfigManager::get_instance().get_str("window_title", "toy3d");
        properties.extent.width = ConfigManager::get_instance().get_int("window_width", 1280);
        properties.extent.height = ConfigManager::get_instance().get_int("window_height", 720);
        properties.vsync = static_cast<Vsync>(ConfigManager::get_instance().get_int("window_vsync", 0));
        properties.mode = static_cast<Mode>(ConfigManager::get_instance().get_int("window_mode", 0));

        // glfw init
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfw_window = glfwCreateWindow(properties.extent.width, properties.extent.height, properties.title.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(glfw_window, this);
        glfwSetFramebufferSizeCallback(glfw_window, frame_buffer_size_cb);

        glfwSetInputMode(glfw_window, GLFW_STICKY_KEYS, 1);
        glfwSetInputMode(glfw_window, GLFW_STICKY_MOUSE_BUTTONS, 1);
    }

	void AndroidWindow::destroy_glfw_window()
	{
        glfwDestroyWindow(glfw_window);
        glfwTerminate();
	}

    void AndroidWindow::resize(uint32_t _width, uint32_t _height)
    {
        IWindow::resize(_width, _height);
    }

    bool AndroidWindow::should_close()
    {
        return glfwWindowShouldClose(glfw_window);
    }

    void AndroidWindow::process_events()
    {
        glfwPollEvents();
    }

    void AndroidWindow::close()
    {
		  glfwSetWindowShouldClose(glfw_window, true);
    }
}