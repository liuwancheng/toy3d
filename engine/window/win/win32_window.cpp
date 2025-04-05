#include "win32_window.h"
#include "core/config/config_manager.h"
#include "core/input/input_system.h"

namespace toy3d
{
    static Win32Window* s_win_instance = nullptr;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (s_win_instance)
        {
            switch (uMsg)
            {
            case WM_CLOSE:
                s_win_instance->should_close();
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            case WM_SIZE:
                // 窗口大小改变时更新尺寸
                s_win_instance->resize(LOWORD(lParam), HIWORD(lParam));
                //LOG_DEBUG("Window resized: {}x{}", s_win_instance->m_width, s_win_instance->m_height);
                return 0;
            }
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    Win32Window::Win32Window(HINSTANCE hInstance)
    {
        if (hInstance == nullptr) 
        {
            hInstance_ = GetModuleHandle(nullptr);
        }
        else 
        {
            hInstance_ = hInstance;
        }
        // 创建窗口
        create_window();
        s_win_instance = this;
    }

    Win32Window::~Win32Window()
    {
        destroy_window();
        s_win_instance = nullptr;
    }

    bool Win32Window::should_close()
    {
        return should_close_;
    }
    void Win32Window::process_events()
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    void Win32Window::close()
    {
        should_close_ = true;
    }

    void Win32Window::resize(uint32_t width, uint32_t height)
    {
        properties_.extent.width = width;
        properties_.extent.height = height;
    }

    void Win32Window::create_window()
    {
        // 获取配置文件中的窗口标题和大小
        properties_.title = ConfigManager::get_instance().get_str("window_title", "toy3d");
        properties_.extent.width = ConfigManager::get_instance().get_int("window_width", 1280);
        properties_.extent.height = ConfigManager::get_instance().get_int("window_height", 720);
        properties_.vsync = static_cast<Vsync>(ConfigManager::get_instance().get_int("window_vsync", 0));
        properties_.mode = static_cast<Mode>(ConfigManager::get_instance().get_int("window_mode", 0));

    
        // 创建窗口类
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance_;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName = nullptr;
        std::wstring wideTitle(properties_.title.begin(), properties_.title.end());
        wc.lpszClassName = wideTitle.c_str();
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    
        if (!RegisterClassEx(&wc))
        {
            //LOG_ERROR("Failed to register window class");
            return ;
        }

        // 调整窗口大小，使客户区达到指定尺寸
        RECT windowRect = { 0, 0, properties_.extent.width, properties_.extent.height };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

        // 创建窗口
        hWnd_ = CreateWindowEx(
            0,                          // 扩展样式
            L"Toy3DWindowClass",         // 窗口类名
            wideTitle.c_str(),              // 窗口标题
            WS_OVERLAPPEDWINDOW,        // 窗口样式
            CW_USEDEFAULT,              // X 位置
            CW_USEDEFAULT,              // Y 位置
            windowRect.right - windowRect.left,  // 宽度
            windowRect.bottom - windowRect.top,  // 高度
            nullptr,                    // 父窗口
            nullptr,                    // 菜单
            hInstance_,                // 实例句柄
            nullptr                     // 附加参数
        );

        if (!hWnd_)
        {
            //LOG_ERROR("Failed to create window");
            return ;
        }

        // 显示窗口
        ShowWindow(hWnd_, SW_SHOW);
        UpdateWindow(hWnd_);
    }

    void Win32Window::destroy_window()
    {
        if (hWnd_) 
        {
            DestroyWindow(hWnd_);
            hWnd_ = nullptr;
        }
    }
} // namespace toy3d