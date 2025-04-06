#include "input_device.h"

namespace toy3d
{
    static int64_t DOUBLE_CLICK_THRESHOLD = 300; // 双击时间阈值，单位为毫秒

    IInputDevice::IInputDevice() 
    {
        key_status_.clear();
        prev_key_status_.clear();
        last_press_time_.clear();
        press_count_.clear();
    }

    void IInputDevice::set_key_status(KeyCode key_code, KeyStatus status) 
    {
        if (key_status_.find(key_code) != key_status_.end()) 
        {
            key_status_[key_code] = status;
        }
    }

    KeyStatus IInputDevice::get_key_status(KeyCode key_code) const 
    { 
        auto it = key_status_.find(key_code);
        if (it != key_status_.end()) 
        {
            return it->second;
        }
        return KeyStatus::None;
    }

    void IInputDevice::update() 
    { 
        // 保存上一帧状态
        prev_key_status_ = key_status_;

        auto now = std::chrono::system_clock::now();
        auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        // 更新按键状态
        for (auto& [key, status] : key_status_) 
        {
            // 根据上一帧状态更新当前状态
            if (status == KeyStatus::Pressed) 
            {
                if (prev_key_status_[key] == KeyStatus::Pressed || 
                    prev_key_status_[key] == KeyStatus::Hold) 
                {
                    // 如果上一帧也是按下状态，则转为Hold状态
                    status = KeyStatus::Hold;
                }
                else 
                {
                    // 处理双击检测
                    int64_t time_since_last_press = current_time - last_press_time_[key];
                    
                    if (time_since_last_press < DOUBLE_CLICK_THRESHOLD) 
                    {
                        status = KeyStatus::DoubleClick;
                        press_count_[key] = 0; // 重置计数
                    }
                    else 
                    {
                        press_count_[key] = 1;
                    }
                    
                    last_press_time_[key] = current_time;
                }
            }
            else if (status == KeyStatus::Released && 
                     (prev_key_status_[key] == KeyStatus::Pressed || 
                      prev_key_status_[key] == KeyStatus::Hold ||
                      prev_key_status_[key] == KeyStatus::DoubleClick)) 
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
            key_status_[code] = KeyStatus::None;
            prev_key_status_[code] = KeyStatus::None;
            last_press_time_[code] = 0.0;
            press_count_[code] = 0;
        }
    }

    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////// 鼠标设备实现 /////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////

    MouseDevice::MouseDevice()
    :IInputDevice(), x_(0), y_(0), prev_x(0), prev_y(0), wheel_delta_(0)
    {
        // 初始化鼠标状态
        for (int i = static_cast<int>(KeyCode::MOUSE_LEFT); i < static_cast<int>(KeyCode::MAX); ++i) 
        {
            KeyCode code = static_cast<KeyCode>(i);
            key_status_[code] = KeyStatus::None;
            prev_key_status_[code] = KeyStatus::None;
            last_press_time_[code] = 0.0;
            press_count_[code] = 0;
        }
    }

    void MouseDevice::process_mouse_move(int x, int y) 
    {
        prev_x = x_;
        prev_y = y_;
        x_ = x;
        y_ = y;
    }


} // namespace toy3d