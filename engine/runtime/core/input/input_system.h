#pragma once
#include "core/misc/pch.h"
#include "input_types.h"
#include "input_device.h"
#include "input_binding.h"

namespace toy3d {

class InputSystem 
{
public:
    static InputSystem& get_instance() 
    {
        static InputSystem instance;
        return instance;
    }
    
    bool init();
    void exit();
    void update();

public:
    std::shared_ptr<KeyboardDevice> get_keyboard_device() { return keyboard_device; }
    std::shared_ptr<MouseDevice> get_mouse_device() { return mouse_device; }

    // ctrl+shift+alt这种可能需要按键判断
    bool is_pressed(KeyCode key_code) const { return keyboard_device->is_key_pressed(key_code); }
    
    // 上下文管理
    InputBindingContext& create_binding_context(const std::string& name, int priority = 0);
    void remove_binding_context(const std::string& name);
    void activate_context(const std::string& name, bool activate);
    
    
    // 事件处理
    void process_event(InputEvent& event);
    
    // 用于绑定持续行为的函数 (类似UE的Axis映射)
    template<typename callback>
    void bind_action(const std::string& context_name, const std::string& action_name, 
        KeyCode key_code, KeyStatus click_status, callback&& cb);
    
    template<typename callback>
    void bind_axis(const std::string& context_name, const std::string& action_name,
        KeyCode key_code, float scale, callback&& cb);

private:
    InputSystem() = default;
    ~InputSystem() = default;
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;
    
    std::shared_ptr<KeyboardDevice> keyboard_device;
    std::shared_ptr<MouseDevice> mouse_device;
    
    std::unordered_map<std::string, InputBindingContext> binding_contexts;
    std::vector<InputBindingContext*> active_mapping_contexts;
    
    void sort_active_mapping_contexts();
};

} // namespace toy3d