#include "input_device.h"

namespace toy3d
{
    static int64_t DOUBLE_CLICK_THRESHOLD = 300; // 双击时间阈值，单位为毫秒

    IInputDevice::IInputDevice() 
    {
        key_status.clear();
        prev_key_status.clear();
        last_press_time.clear();
        press_count.clear();
    }

    void IInputDevice::set_key_status(KeyCode key_code, KeyStatus status) 
    {
        if (key_status.find(key_code) != key_status.end()) 
        {
            key_status[key_code] = status;
        }
    }

    KeyStatus IInputDevice::get_key_status(KeyCode key_code) const 
    { 
        auto it = key_status.find(key_code);
        if (it != key_status.end()) 
        {
            return it->second;
        }
        return KeyStatus::None;
    }

    void IInputDevice::update() 
    { 
        // 保存上一帧状态
        prev_key_status = key_status;

        auto now = std::chrono::system_clock::now();
        auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        // 更新按键状态
        for (auto& [key, status] : key_status) 
        {
            // 根据上一帧状态更新当前状态
            if (status == KeyStatus::Pressed) 
            {
                if (prev_key_status[key] == KeyStatus::Pressed || 
                    prev_key_status[key] == KeyStatus::Hold) 
                {
                    // 如果上一帧也是按下状态，则转为Hold状态
                    status = KeyStatus::Hold;
                }
                else 
                {
                    // 处理双击检测
                    int64_t time_since_last_press = current_time - last_press_time[key];
                    
                    if (time_since_last_press < DOUBLE_CLICK_THRESHOLD) 
                    {
                        status = KeyStatus::DoubleClick;
                        press_count[key] = 0; // 重置计数
                    }
                    else 
                    {
                        press_count[key] = 1;
                    }
                    
                    last_press_time[key] = current_time;
                }
            }
            else if (status == KeyStatus::Released && 
                     (prev_key_status[key] == KeyStatus::Pressed || 
                      prev_key_status[key] == KeyStatus::Hold ||
                      prev_key_status[key] == KeyStatus::DoubleClick)) 
            {
                // 确保从按下到释放的状态转换正确
                status = KeyStatus::None;
            }
        }
    }

    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////// 键盘设备实现 /////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////
    KeyboardDevice::KeyboardDevice()
        :IInputDevice()
    {
        // 初始化所有键的状态
        for (int i = static_cast<int>(KeyCode::A); i < static_cast<int>(KeyCode::MAX); ++i) 
        {
            KeyCode code = static_cast<KeyCode>(i);
            key_status[code] = KeyStatus::None;
            prev_key_status[code] = KeyStatus::None;
            last_press_time[code] = 0.0;
            press_count[code] = 0;
        }
    }

    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////// 鼠标设备实现 /////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////

    MouseDevice::MouseDevice()
    :IInputDevice(), x(0), y(0), prev_x(0), prev_y(0), wheel_delta(0)
    {
        // 初始化鼠标状态
        for (int i = static_cast<int>(KeyCode::MOUSE_LEFT); i < static_cast<int>(KeyCode::MAX); ++i) 
        {
            KeyCode code = static_cast<KeyCode>(i);
            key_status[code] = KeyStatus::None;
            prev_key_status[code] = KeyStatus::None;
            last_press_time[code] = 0.0;
            press_count[code] = 0;
        }
    }

    void MouseDevice::process_mouse_move(int _x, int _y) 
    {
        prev_x = x;
        prev_y = y;
        x = _x;
        y = _y;
    }


} // namespace toy3d
