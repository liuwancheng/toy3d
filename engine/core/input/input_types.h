#pragma once
#include "pch.h"

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
        MouseWheel
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
        float scale; // 用于Axis绑定的持续变化数据
        InputEventType type;
    };
    
    // 键盘事件
    struct KeyEvent : InputEvent 
    {
        KeyCode key_code; // 键码
    };
    
    // 鼠标点击事件
    struct MouseButtonEvent : InputEvent 
    {
        KeyCode key_code; // 键码
        int x;
        int y;
    };
    
    // 鼠标移动事件
    struct MouseMoveEvent : InputEvent 
    {
        int x;
        int y;
        int delta_x;
        int delta_y;
    };
    
    // 鼠标滚轮事件
    struct MouseWheelEvent : InputEvent 
    {
        int x;
        int y;
        int delta;
    };
    
} // namespace toy3d