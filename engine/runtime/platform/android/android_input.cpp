#include "android_input.h"
#include "core/input/input_system.h"

namespace toy3d
{
    static std::map<int, KeyCode> glfw2keycode = {
        // 字母键
        {GLFW_KEY_A, KeyCode::A},
        {GLFW_KEY_B, KeyCode::B},
        {GLFW_KEY_C, KeyCode::C},
        {GLFW_KEY_D, KeyCode::D},
        {GLFW_KEY_E, KeyCode::E},
        {GLFW_KEY_F, KeyCode::F},
        {GLFW_KEY_G, KeyCode::G},
        {GLFW_KEY_H, KeyCode::H},
        {GLFW_KEY_I, KeyCode::I},
        {GLFW_KEY_J, KeyCode::J},
        {GLFW_KEY_K, KeyCode::K},
        {GLFW_KEY_L, KeyCode::L},
        {GLFW_KEY_M, KeyCode::M},
        {GLFW_KEY_N, KeyCode::N},
        {GLFW_KEY_O, KeyCode::O},
        {GLFW_KEY_P, KeyCode::P},
        {GLFW_KEY_Q, KeyCode::Q},
        {GLFW_KEY_R, KeyCode::R},
        {GLFW_KEY_S, KeyCode::S},
        {GLFW_KEY_T, KeyCode::T},
        {GLFW_KEY_U, KeyCode::U},
        {GLFW_KEY_V, KeyCode::V},
        {GLFW_KEY_W, KeyCode::W},
        {GLFW_KEY_X, KeyCode::X},
        {GLFW_KEY_Y, KeyCode::Y},
        {GLFW_KEY_Z, KeyCode::Z},
        
        // 数字键
        {GLFW_KEY_0, KeyCode::NUM_0},
        {GLFW_KEY_1, KeyCode::NUM_1},
        {GLFW_KEY_2, KeyCode::NUM_2},
        {GLFW_KEY_3, KeyCode::NUM_3},
        {GLFW_KEY_4, KeyCode::NUM_4},
        {GLFW_KEY_5, KeyCode::NUM_5},
        {GLFW_KEY_6, KeyCode::NUM_6},
        {GLFW_KEY_7, KeyCode::NUM_7},
        {GLFW_KEY_8, KeyCode::NUM_8},
        {GLFW_KEY_9, KeyCode::NUM_9},
        
        // 功能键
        {GLFW_KEY_ESCAPE, KeyCode::ESCAPE},
        {GLFW_KEY_ENTER, KeyCode::ENTER},
        {GLFW_KEY_TAB, KeyCode::TAB},
        {GLFW_KEY_BACKSPACE, KeyCode::BACKSPACE},
        {GLFW_KEY_RIGHT, KeyCode::RIGHT},
        {GLFW_KEY_LEFT, KeyCode::LEFT},
        {GLFW_KEY_DOWN, KeyCode::DOWN},
        {GLFW_KEY_UP, KeyCode::UP},
        {GLFW_KEY_PAGE_UP, KeyCode::PAGE_UP},
        {GLFW_KEY_PAGE_DOWN, KeyCode::PAGE_DOWN},
        
        // 修饰键
        {GLFW_KEY_LEFT_SHIFT, KeyCode::SHIFT},
        {GLFW_KEY_RIGHT_SHIFT, KeyCode::SHIFT},
        {GLFW_KEY_LEFT_CONTROL, KeyCode::CTRL},
        {GLFW_KEY_RIGHT_CONTROL, KeyCode::CTRL},
        {GLFW_KEY_LEFT_ALT, KeyCode::ALT},
        {GLFW_KEY_RIGHT_ALT, KeyCode::ALT},
        
        // 特殊键
        {GLFW_KEY_SPACE, KeyCode::SPACE},
        
        // 小键盘
        {GLFW_KEY_KP_0, KeyCode::KP_0},
        {GLFW_KEY_KP_1, KeyCode::KP_1},
        {GLFW_KEY_KP_2, KeyCode::KP_2},
        {GLFW_KEY_KP_3, KeyCode::KP_3},
        {GLFW_KEY_KP_4, KeyCode::KP_4},
        {GLFW_KEY_KP_5, KeyCode::KP_5},
        {GLFW_KEY_KP_6, KeyCode::KP_6},
        {GLFW_KEY_KP_7, KeyCode::KP_7},
        {GLFW_KEY_KP_8, KeyCode::KP_8},
        {GLFW_KEY_KP_9, KeyCode::KP_9},

        // 鼠标按键
        {GLFW_MOUSE_BUTTON_LEFT, KeyCode::MOUSE_LEFT},
        {GLFW_MOUSE_BUTTON_RIGHT, KeyCode::MOUSE_RIGHT},
        {GLFW_MOUSE_BUTTON_MIDDLE, KeyCode::MOUSE_MIDDLE},
        {GLFW_MOUSE_BUTTON_4, KeyCode::MOUSE_4},
        {GLFW_MOUSE_BUTTON_5, KeyCode::MOUSE_5}
    };

    bool AndroidPlatformInput::init()
    {
        // 设置键盘回调
        glfwSetKeyCallback(glfw_window, AndroidPlatformInput::key_callback);
        
        // 设置鼠标按钮回调
        glfwSetMouseButtonCallback(glfw_window, AndroidPlatformInput::mousebutton_callback);
        
        // 设置鼠标移动回调
        glfwSetCursorPosCallback(glfw_window, AndroidPlatformInput::mousemove_callback);
        
        // 设置鼠标滚轮回调
        glfwSetScrollCallback(glfw_window, AndroidPlatformInput::mousewheel_callback);

        InputSystem::get_instance().init();

        return true;
    }

    void AndroidPlatformInput::update()
    {
        // 更新输入设备状态
        InputSystem::get_instance().update();
    }

    void AndroidPlatformInput::exit()
    {
        InputSystem::get_instance().exit();
    }


    void AndroidPlatformInput::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        KeyCode key_code = KeyCode::MAX;
        auto key_code_it = glfw2keycode.find(key);
        if (key_code_it != glfw2keycode.end()) 
        {
            key_code = key_code_it->second;
        } 
        else 
        {
            return; // 如果没有找到对应的键码，直接返回
        }
    
        auto keyboard = InputSystem::get_instance().get_keyboard_device();   
        KeyEvent event;
        event.key_code = key_code;
        if (action == GLFW_PRESS) 
        {
            keyboard->set_key_status(key_code, KeyStatus::Pressed);
            event.type = InputEventType::KeyPressed;
        } 
        else if (action == GLFW_REPEAT) 
        {
            keyboard->set_key_status(key_code, KeyStatus::Hold);
            event.type = InputEventType::KeyHold;
        }
        else if (action == GLFW_RELEASE) 
        {
            keyboard->set_key_status(key_code, KeyStatus::Released);
            event.type = InputEventType::KeyReleased;
        }

        InputSystem::get_instance().process_event(event);
    }

    void AndroidPlatformInput::mousebutton_callback(GLFWwindow* window, int button, int action, int mods)
    {
       auto mouse = InputSystem::get_instance().get_mouse_device();
        
        KeyCode key_code; 
        // 映射鼠标按钮
        switch (button)
        {
            case GLFW_MOUSE_BUTTON_LEFT:
                key_code = KeyCode::MOUSE_LEFT;
                break;
            case GLFW_MOUSE_BUTTON_RIGHT:
                key_code = KeyCode::MOUSE_RIGHT;
                break;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                key_code = KeyCode::MOUSE_MIDDLE;
                break;
            case GLFW_MOUSE_BUTTON_4:
                key_code = KeyCode::MOUSE_4;
                break;
            case GLFW_MOUSE_BUTTON_5:
                key_code = KeyCode::MOUSE_5;
                break;
            default:
                key_code = KeyCode::MAX;
                break;
        }
        if(key_code == KeyCode::MAX) 
        {
            return; // 如果没有找到对应的键码，直接返回
        }
       
        KeyEvent event;
        event.key_code = key_code;
        if (action == GLFW_PRESS) 
        {
            mouse->set_key_status(key_code, KeyStatus::Pressed);
            event.type = InputEventType::MouseButtonPressed;
        } 
        else if (action == GLFW_REPEAT) 
        {
            mouse->set_key_status(key_code, KeyStatus::Hold);
            event.type = InputEventType::MouseButtonHold;
        }
        else if (action == GLFW_RELEASE) 
        {
            mouse->set_key_status(key_code, KeyStatus::Released);
            event.type = InputEventType::MouseButtonReleased;
        }
        InputSystem::get_instance().process_event(event);
    }

    void AndroidPlatformInput::mousemove_callback(GLFWwindow* window, double xpos, double ypos)
    {
        auto mouse = InputSystem::get_instance().get_mouse_device();
        mouse->process_mouse_move(static_cast<int>(xpos), static_cast<int>(ypos));
        
        MouseMoveEvent event;
        event.type = InputEventType::MouseMove;
        event.x = static_cast<int>(xpos);
        event.y = static_cast<int>(ypos);
        
        int delta_x, delta_y;
        mouse->get_mouse_delta(delta_x, delta_y);
        event.delta_x = delta_x;
        event.delta_y = delta_y;
        
        InputSystem::get_instance().process_event(event);
    }

    void AndroidPlatformInput::mousewheel_callback(GLFWwindow* window, double xoffset, double yoffset)
    {
        auto mouse = InputSystem::get_instance().get_mouse_device();
        mouse->process_mouse_wheel(static_cast<int>(yoffset));
        
        MouseWheelEvent event;
        event.type = InputEventType::MouseWheel;
        event.delta = xoffset + yoffset;
        
        InputSystem::get_instance().process_event(event);
    }
}// namespace toy3d