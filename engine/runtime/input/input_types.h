#pragma once
#include "runtime_pch.h"

namespace toy3d
{
    // 跨平台键码枚举
    enum class KeyCode 
    {
        // 字母键
        A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,

        // 数字键
        NUM_0,NUM_1,NUM_2,NUM_3,NUM_4,NUM_5,NUM_6,NUM_7,NUM_8,NUM_9,

        // 功能键
        ESCAPE,ENTER,TAB,BACKSPACE,
        RIGHT,LEFT,DOWN,UP,
        PAGE_UP,PAGE_DOWN,

        // 修饰键
        SHIFT,
        CTRL,
        ALT,

        // 特殊键
        SPACE,

        // 小键盘
        KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,

        // 鼠标按键
        MOUSE_LEFT,MOUSE_RIGHT,MOUSE_MIDDLE,MOUSE_4,MOUSE_5,
        MAX,

        // 鼠标移动和滚动事件特殊处理
        MOUSE_MOVE,
        MOUSE_WHEEL
    };

    // 输入事件类型
    enum class InputEventType 
    {
        KeyPressed,
        KeyReleased,
        KeyHold,
        KeyDoubleClick,

        MouseButtonPressed,
        MouseButtonReleased,
        MouseButtonHold,
        MouseButtonDoubleClick,

        MouseMove,
        MouseWheel,
        TextInput,
        WindowFocus
    };

    enum class KeyStatus 
    {
        Pressed,     // 按下时触发一次
        Released,    // 释放时触发一次
        Hold,        // 按住时每帧触发
        DoubleClick, // 双击触发
        None         // 无状态
    };
    
    struct InputEvent 
    {
        float scale = 0.0F; // 用于Axis绑定的持续变化数据
        InputEventType type = InputEventType::KeyPressed;
    };
    
    // 键盘事件
    struct KeyEvent : InputEvent 
    {
        KeyCode key_code = KeyCode::MAX; // 键码
    };
    
    // 鼠标点击事件
    struct MouseButtonEvent : InputEvent 
    {
        KeyCode key_code = KeyCode::MAX; // 键码
        int x = 0;
        int y = 0;
    };
    
    // 鼠标移动事件
    struct MouseMoveEvent : InputEvent 
    {
        int x = 0;
        int y = 0;
        int delta_x = 0;
        int delta_y = 0;
    };
    
    // 鼠标滚轮事件
    struct MouseWheelEvent : InputEvent 
    {
        int x = 0;
        int y = 0;
        float delta_x = 0.0F;
        float delta_y = 0.0F;
    };

    constexpr bool is_unicode_scalar(std::uint32_t code_point) noexcept
    {
        return code_point <= 0x10FFFFu &&
            !(code_point >= 0xD800u && code_point <= 0xDFFFu);
    }

    struct TextInputEvent : InputEvent
    {
        explicit TextInputEvent(std::uint32_t value = 0u)
            : code_point(value)
        {
            type = InputEventType::TextInput;
        }

        bool valid() const noexcept { return is_unicode_scalar(code_point); }
        std::uint32_t code_point = 0u;
    };

    struct WindowFocusEvent : InputEvent
    {
        explicit WindowFocusEvent(bool value = false)
            : focused(value)
        {
            type = InputEventType::WindowFocus;
        }

        bool focused = false;
    };
    
} // namespace toy3d
