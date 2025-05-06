#include "win32_window.h"
#include "core/config/config_manager.h"
#include "core/input/input_system.h"
#include "win32_input.h"
#include "resource.h"

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
                s_win_instance->close();
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

        if (s_win_instance)
        {
            Win32PlatformInput* win32_input = static_cast<Win32PlatformInput*>(s_win_instance->get_platform_input());
            win32_input->process_win32_msg(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    Win32Window::Win32Window(HINSTANCE _hInstance):IWindow()
    {
        if (_hInstance == nullptr) 
        {
            hInstance = GetModuleHandle(nullptr);
        }
        else 
        {
            hInstance = _hInstance;
        }
        // 创建窗口
        create_window();
        s_win_instance = this;

        // 初始化InputSystem
        platform_input = std::make_unique<Win32PlatformInput>();
        if (!platform_input->init()) 
        {
            //LOG_ERROR("Failed to initialize platform input system");
            return ;
        }
    }

    Win32Window::~Win32Window()
    {
        destroy_window();
        s_win_instance = nullptr;
    }

    bool Win32Window::should_close()
    {
        return b_close;
    }
    void Win32Window::process_events()
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        platform_input->update();
    }
    void Win32Window::close()
    {
        b_close = true;
    }

    void Win32Window::resize(uint32_t _width, uint32_t _height)
    {
        properties.extent.width = _width;
        properties.extent.height = _height;
    }

    void Win32Window::create_window()
    {
        // 获取配置文件中的窗口标题和大小
        properties.title = ConfigManager::get_instance().get_str("window_title", "toy3d");
        properties.extent.width = ConfigManager::get_instance().get_int("window_width", 1280);
        properties.extent.height = ConfigManager::get_instance().get_int("window_height", 720);
        properties.vsync = static_cast<Vsync>(ConfigManager::get_instance().get_int("window_vsync", 0));
        properties.mode = static_cast<Mode>(ConfigManager::get_instance().get_int("window_mode", 0));

    
        HICON hIcon = static_cast<HICON>(::LoadImage(hInstance,
            MAKEINTRESOURCE(IDI_TOY3D_ICON),
            IMAGE_ICON,
            128, 128,
            LR_DEFAULTCOLOR));
        HICON hIconSm = static_cast<HICON>(::LoadImage(hInstance,
            MAKEINTRESOURCE(IDI_TOY3D_ICON),
            IMAGE_ICON,
            32, 32,
            LR_DEFAULTCOLOR));

        // 创建窗口类
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = hIcon;
        wc.hIconSm = hIconSm;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "Win32Window";
    
        if (!RegisterClassEx(&wc))
        {
            //LOG_ERROR("Failed to register window class");
            return ;
        }

        // 调整窗口大小，使客户区达到指定尺寸
        RECT windowRect = { 0, 0, properties.extent.width, properties.extent.height };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

        // 创建窗口
        hWnd = CreateWindowEx(
            0,                          // 扩展样式
            "Win32Window",        // 窗口类名
            properties.title.c_str(),  // 窗口标题
            WS_OVERLAPPEDWINDOW,        // 窗口样式
            CW_USEDEFAULT,              // X 位置
            CW_USEDEFAULT,              // Y 位置
            windowRect.right - windowRect.left,  // 宽度
            windowRect.bottom - windowRect.top,  // 高度
            nullptr,                    // 父窗口
            nullptr,                    // 菜单
            hInstance,                 // 实例句柄
            this                        // 附加参数
        );

        if (!hWnd)
        {
            //LOG_ERROR("Failed to create window");

            DWORD error = GetLastError();
            LPVOID lpMsgBuf;

            FormatMessage(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&lpMsgBuf,
                0, NULL);

            // 输出错误信息到控制台或调试窗口
            OutputDebugString((LPCTSTR)lpMsgBuf);
            MessageBox(NULL, (LPCTSTR)lpMsgBuf, "创建窗口失败", MB_OK | MB_ICONERROR);

            // 释放消息缓冲区
            LocalFree(lpMsgBuf);
            return ;
        }

        // 显示窗口
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
    }

    void Win32Window::destroy_window()
    {
        if (hWnd) 
        {
            DestroyWindow(hWnd);
            hWnd = nullptr;
        }
    }
} // namespace toy3d