#include "mac_window.h"

namespace toy3d
{
    static void frame_buffer_size_cb(GLFWwindow* window, int width, int height)
    {
        MacWindow* win = reinterpret_cast<MacWindow*>(glfwGetWindowUserPointer(window));
        win->resize(width, height);
    }

    MacWindow::MacWindow(uint32_t width, uint32_t height)
    :IWindow(width, height)
    {
        // glfw init
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_glfw_win_handle = glfwCreateWindow(width, height, m_win_name.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(m_glfw_win_handle, this);
        glfwSetFramebufferSizeCallback(m_glfw_win_handle, frame_buffer_size_cb);


        // glfwSetWindowCloseCallback(m_glfw_win_handle, window_close_callback);
        // glfwSetWindowSizeCallback(m_glfw_win_handle, window_size_callback);
        // glfwSetWindowFocusCallback(m_glfw_win_handle, window_focus_callback);
        // glfwSetKeyCallback(m_glfw_win_handle, key_callback);
        // glfwSetCursorPosCallback(m_glfw_win_handle, cursor_position_callback);
        // glfwSetMouseButtonCallback(m_glfw_win_handle, mouse_button_callback);

        glfwSetInputMode(m_glfw_win_handle, GLFW_STICKY_KEYS, 1);
        glfwSetInputMode(m_glfw_win_handle, GLFW_STICKY_MOUSE_BUTTONS, 1);
    }

    MacWindow::~MacWindow()
    {
        glfwDestroyWindow(m_glfw_win_handle);
        glfwTerminate();
    }

    void MacWindow::resize(uint32_t width, uint32_t height)
    {
        IWindow::resize(width, height);
    }

    bool MacWindow::should_close()
    {
        return glfwWindowShouldClose(m_glfw_win_handle);
    }

    void MacWindow::process_events()
    {
        glfwPollEvents();
    }

    void MacWindow::close()
    {
    }

    void MacWindow::get_reequired_extensions(const char** ex_name_list, uint32_t* count)
    {
        ex_name_list = glfwGetRequiredInstanceExtensions(count);
    }

    void MacWindow::create_window_surface(VkInstance instance, VkSurfaceKHR & surface)
    {
        if(glfwCreateWindowSurface(instance, m_glfw_win_handle, nullptr, &surface) != VK_SUCCESS)
        {
            std::cout << "failed to create vk surface" << std::endl;
        }
    }
}