#pragma once
#include "runtime_pch.h"
#include "input_types.h"

namespace toy3d
{
    // 输入设备接口
    class IInputDevice
    {
      public:
        IInputDevice();
        virtual ~IInputDevice() {};

        virtual const char* get_device_name() const = 0;

      public:
        virtual void update();
        virtual void clear_pressed_state() noexcept;

        virtual void set_key_status(KeyCode key_code, KeyStatus status);
        virtual KeyStatus get_key_status(KeyCode key_code) const;

        virtual bool is_key_pressed(KeyCode key_code) const { return get_key_status(key_code) == KeyStatus::Pressed; }
        virtual bool is_key_released(KeyCode key_code) const { return get_key_status(key_code) == KeyStatus::Released; }
        virtual bool is_key_hold(KeyCode key_code) const { return get_key_status(key_code) == KeyStatus::Hold; }
        virtual bool is_key_double_click(KeyCode key_code) const
        {
            return get_key_status(key_code) == KeyStatus::DoubleClick;
        }

      protected:
        std::map<KeyCode, KeyStatus> key_status;
        std::map<KeyCode, KeyStatus> prev_key_status;
        std::map<KeyCode, double> last_press_time;
        std::map<KeyCode, int> press_count;
    };

    // 键盘设备类
    class KeyboardDevice : public IInputDevice
    {
      public:
        KeyboardDevice();
        virtual const char* get_device_name() const override { return "Keyboard"; }
    };

    // 鼠标设备类
    class MouseDevice : public IInputDevice
    {
      private:
        int x, y;
        int prev_x, prev_y;
        int wheel_delta;

      public:
        MouseDevice();
        const char* get_device_name() const override { return "Mouse"; }

        void process_mouse_move(int _x, int _y);
        void process_mouse_wheel(int delta) { wheel_delta = delta; };
        void clear_pressed_state() noexcept override;

        void get_mouse_pos(int& out_x, int& out_y) const
        {
            out_x = x;
            out_y = y;
        };
        void get_mouse_delta(int& dx, int& dy) const
        {
            dx = x - prev_x;
            dy = y - prev_y;
        };
        int get_wheel_delta() const { return wheel_delta; }
    };

} // namespace toy3d
