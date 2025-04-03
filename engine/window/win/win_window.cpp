#include "win_window.h"

namespace toy3d
{
    static WinWindow* s_win_instance = nullptr;

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

    WinWindow::WinWindow((HINSTANCE hInstance = nullptr))
    {
        if (hInstance == nullptr) 
        {
            _hInstance = GetModuleHandle(nullptr);
        }
        else 
        {
            _hInstance = hInstance;
        }
        // 创建窗口
        create_window();
        s_win_instance = this;
    }

    WinWindow::~WinWindow()
    {
        destroy_window();
        s_win_instance = nullptr;
    }

    bool WinWindow::should_close()
    {
        return _should_close;
    }
    void WinWindow::process_events()
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    void WinWindow::close()
    {
        _should_close = true;
    }

    void WinWindow::resize(uint32_t width, uint32_t height)
    {
        _properties.extent.width = width;
        _properties.extent.height = height;
    }

    void WinWindow::create_window()
    {
        // 获取配置文件中的窗口标题和大小
        _properties.title = ConfigManager::get_instance().get_str("window_title", "toy3d");
        _properties.extent.width = ConfigManager::get_instance().get_int("window_width", 1280);
        _properties.extent.height = ConfigManager::get_instance().get_int("window_height", 720);
        _properties.vsync = static_cast<Vsync>(ConfigManager::get_instance().get_int("window_vsync", 0));
        _properties.mode = static_cast<Mode>(ConfigManager::get_instance().get_int("window_mode", 0));

    
        // 创建窗口类
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = _properties.title.c_str();
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    
        if (!RegisterClassEx(&wc))
        {
            //LOG_ERROR("Failed to register window class");
            return ;
        }

        // 调整窗口大小，使客户区达到指定尺寸
        RECT windowRect = { 0, 0, _properties.extent.width, _properties.extent.height };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

        // 创建窗口
        _hWnd = CreateWindowEx(
            0,                          // 扩展样式
            "Toy3DWindowClass",         // 窗口类名
            title,                      // 窗口标题
            WS_OVERLAPPEDWINDOW,        // 窗口样式
            CW_USEDEFAULT,              // X 位置
            CW_USEDEFAULT,              // Y 位置
            windowRect.right - windowRect.left,  // 宽度
            windowRect.bottom - windowRect.top,  // 高度
            nullptr,                    // 父窗口
            nullptr,                    // 菜单
            hInstance,                // 实例句柄
            nullptr                     // 附加参数
        );

        if (!_hWnd)
        {
            //LOG_ERROR("Failed to create window");
            return ;
        }

        // 显示窗口
        ShowWindow(_hWnd, SW_SHOW);
        UpdateWindow(_hWnd);
    }

    void WinWindow::destroy_window()
    {
        if (_hWnd) 
        {
            DestroyWindow(_hWnd);
            _hWnd = nullptr;
        }
    }
} // namespace toy3d