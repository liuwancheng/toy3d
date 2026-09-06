#pragma once
#include "runtime_pch.h"
#include "input_types.h"
#include "input_device.h"
#include "input_binding.h"

#include <functional>

namespace toy3d {

class InputSystem 
{
public:
    struct CapturePolicy
    {
        bool mouse = false;
        bool keyboard = false;
        bool text = false;
    };

    using EventSink = std::function<void(const InputEvent&)>;

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
    void process_event(const InputEvent& event);
    void set_event_sink(EventSink sink);
    void set_capture_policy(CapturePolicy policy) noexcept;
    void clear_pressed_state() noexcept;
    
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
    EventSink event_sink_;
    CapturePolicy capture_policy_;
    
    void sort_active_mapping_contexts();
};

template<typename callback>
void InputSystem::bind_action(const std::string& context_name, const std::string& action_name,
    KeyCode key_code, KeyStatus click_status, callback&& cb)
{
    auto it = binding_contexts.find(context_name);
    if (it == binding_contexts.end()) return;

    auto& action = it->second.create_action(action_name);
    action.add_binding(key_code, click_status, std::forward<callback>(cb));
}

template<typename callback>
void InputSystem::bind_axis(const std::string& context_name, const std::string& action_name,
    KeyCode key_code, float scale, callback&& cb)
{
    auto it = binding_contexts.find(context_name);
    if (it == binding_contexts.end()) return;

    auto& action = it->second.create_action(action_name);
    action.add_axis_binding(key_code, scale, std::forward<callback>(cb));
}

} // namespace toy3d
