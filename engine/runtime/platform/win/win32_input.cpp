#include "win32_input.h"
#include <windows.h>
#include <windowsx.h>

#include "core/input/input_system.h"

namespace toy3d
{
    static std::map<int, KeyCode> win2keycode =
    {
        // 字母键
        {0x41, KeyCode::A}, {0x42, KeyCode::B}, {0x43, KeyCode::C}, {0x44, KeyCode::D},
        {0x45, KeyCode::E}, {0x46, KeyCode::F}, {0x47, KeyCode::G}, {0x48, KeyCode::H},
        {0x49, KeyCode::I}, {0x4A, KeyCode::J}, {0x4B, KeyCode::K}, {0x4C, KeyCode::L},
        {0x4D, KeyCode::M}, {0x4E, KeyCode::N}, {0x4F, KeyCode::O}, {0x50, KeyCode::P},
        {0x51, KeyCode::Q}, {0x52, KeyCode::R}, {0x53, KeyCode::S}, {0x54, KeyCode::T},
        {0x55, KeyCode::U}, {0x56, KeyCode::V}, {0x57, KeyCode::W}, {0x58, KeyCode::X},
        {0x59, KeyCode::Y}, {0x5A, KeyCode::Z},

        // 数字键 (主键盘)
        {0x30, KeyCode::NUM_0}, {0x31, KeyCode::NUM_1}, {0x32, KeyCode::NUM_2},
        {0x33, KeyCode::NUM_3}, {0x34, KeyCode::NUM_4}, {0x35, KeyCode::NUM_5},
        {0x36, KeyCode::NUM_6}, {0x37, KeyCode::NUM_7}, {0x38, KeyCode::NUM_8},
        {0x39, KeyCode::NUM_9},

        // 功能键
        {VK_ESCAPE, KeyCode::ESCAPE},
        {VK_RETURN, KeyCode::ENTER},
        {VK_TAB, KeyCode::TAB},
        {VK_BACK, KeyCode::BACKSPACE},
        {VK_RIGHT, KeyCode::RIGHT},
        {VK_LEFT, KeyCode::LEFT},
        {VK_DOWN, KeyCode::DOWN},
        {VK_UP, KeyCode::UP},
        {VK_PRIOR, KeyCode::PAGE_UP},
        {VK_NEXT, KeyCode::PAGE_DOWN},

        // 修饰键
        {VK_LSHIFT, KeyCode::SHIFT},
        {VK_LCONTROL, KeyCode::CTRL},
        {VK_LMENU, KeyCode::ALT},
        {VK_RSHIFT, KeyCode::SHIFT},
        {VK_RCONTROL, KeyCode::CTRL},
        {VK_RMENU, KeyCode::ALT},

        // 鼠标按键 (这些在Windows消息中通常不使用VK_codes，而是在WM_MOUSE消息中处理)
        {VK_LBUTTON, KeyCode::MOUSE_LEFT},
        {VK_RBUTTON, KeyCode::MOUSE_RIGHT},
        {VK_MBUTTON, KeyCode::MOUSE_MIDDLE},
        {VK_XBUTTON1, KeyCode::MOUSE_4},
        {VK_XBUTTON2, KeyCode::MOUSE_5},

        // 特殊键
        {VK_SPACE, KeyCode::SPACE},

        // 小键盘数字键
        {VK_NUMPAD0, KeyCode::KP_0},
        {VK_NUMPAD1, KeyCode::KP_1},
        {VK_NUMPAD2, KeyCode::KP_2},
        {VK_NUMPAD3, KeyCode::KP_3},
        {VK_NUMPAD4, KeyCode::KP_4},
        {VK_NUMPAD5, KeyCode::KP_5},
        {VK_NUMPAD6, KeyCode::KP_6},
        {VK_NUMPAD7, KeyCode::KP_7},
        {VK_NUMPAD8, KeyCode::KP_8},
        {VK_NUMPAD9, KeyCode::KP_9},
    };

    bool Win32PlatformInput::init() 
    {
        // 初始化输入系统
        InputSystem::get_instance().init();
        return true;
    }

    void Win32PlatformInput::update() 
    {
        // 更新输入设备状态
        InputSystem::get_instance().update();
    }

    void Win32PlatformInput::exit() 
    {
        // 清理Win32输入系统
        // 这里可以进行一些Win32特定的清理操作
        InputSystem::get_instance().exit();
    }

    void Win32PlatformInput::process_win32_msg(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) 
    {
        auto mouse = InputSystem::get_instance().get_mouse_device();
        auto keyboard = InputSystem::get_instance().get_keyboard_device();

        KeyCode key_code = KeyCode::MAX;
        if (!(message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL))
        {
            auto _key_code_iter = win2keycode.find(static_cast<int>(wParam));
            if (_key_code_iter == win2keycode.end()) 
            {
                return;
            }
            key_code = _key_code_iter->second;
        }



        switch (message) 
        {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (keyboard) 
                {
                    keyboard->set_key_status(key_code, KeyStatus::Pressed);
                
                    // 传递给输入系统层处理
                    KeyEvent event;
                    event.type = InputEventType::KeyPressed;
                    event.key_code = key_code;
                    //event.is_repeat = (lParam & 0x40000000) != 0;
                    //event.timestamp = GetTickCount64() / 1000.0;
                
                    InputSystem::get_instance().process_event(event);
                }
                break;
            
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (keyboard) {
                    keyboard->set_key_status(key_code, KeyStatus::Released);
                
                    KeyEvent event;
                    event.type = InputEventType::KeyReleased;
                    event.key_code = key_code;              
                    InputSystem::get_instance().process_event(event);
                }
                break;
            // 鼠标移动事件
            case WM_MOUSEMOVE:
                if (mouse) 
                {
                    int _x = GET_X_LPARAM(lParam);
                    int _y = GET_Y_LPARAM(lParam);
                    mouse->process_mouse_move(_x, _y);
                
                    MouseMoveEvent event;
                    event.type = InputEventType::MouseMove;
                    event.x = _x;
                    event.y = _y;
                    mouse->get_mouse_delta(event.delta_x, event.delta_y);
                   // event.timestamp = GetTickCount64() / 1000.0;
                
                    InputSystem::get_instance().process_event(event);
                }
                break;
            // 鼠标滚轮事件
            case WM_MOUSEWHEEL:
                if (mouse) 
                {
                    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                    mouse->process_mouse_wheel(delta);
                
                    MouseWheelEvent event;
                    event.type = InputEventType::MouseWheel;
                    event.delta = delta;
                    //event.timestamp = GetTickCount64() / 1000.0;
                
                    InputSystem::get_instance().process_event(event);
                }
                break;
            
            // 鼠标按钮事件
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                if (mouse) 
                {
                    mouse->set_key_status(key_code, KeyStatus::Pressed);
                
                    MouseButtonEvent event;
                    event.type = InputEventType::MouseButtonPressed;
                    //event.timestamp = GetTickCount64() / 1000.0;
                
                    InputSystem::get_instance().process_event(event);
                }
                break;
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
                if (mouse) 
                {
                    mouse->set_key_status(key_code, KeyStatus::Released);
                
                    MouseButtonEvent event;
                    event.type = InputEventType::MouseButtonReleased;
                    //event.timestamp = GetTickCount64() / 1000.0;
                
                    InputSystem::get_instance().process_event(event);
                }
                break;
        
        }
    }
} //namespace toy3d