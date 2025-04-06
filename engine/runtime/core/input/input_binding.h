#pragma once
#include "core/misc/pch.h"
#include "input_types.h"

namespace toy3d
{
using InputActionCallback = std::function<void(const InputEvent&)>;

struct InputBinding 
{
    KeyCode key_code;               // 按键代码
    KeyStatus input_state;          // 触发方式
    InputActionCallback callback;   // 回调函数

    float scale;
};

class InputAction 
{
private:
    std::string action_name_;
    std::vector<InputBinding> bindings;

public:
	InputAction() = default;
    InputAction(const std::string& name) : action_name_(name) {}
    
    // 用于处理单击事件，比如开火、跳跃等
    void add_binding(KeyCode _key_code, KeyStatus _key_status, InputActionCallback callback)
    {
        InputBinding binding;
        binding.key_code = _key_code;
        binding.input_state = _key_status;
        binding.callback = callback;
        bindings.push_back(binding);
    }

    // 用于处理持续行为，比如移动、旋转等
    void add_axis_binding(KeyCode _key_code, float scale, InputActionCallback callback)
    {
        InputBinding binding;
        binding.key_code = _key_code;
        binding.input_state = KeyStatus::Hold; // 持续行为通常是按住状态
        binding.scale = scale;
        binding.callback = callback;
        bindings.push_back(binding);
    }

    const std::string& get_name() const { return action_name_; }
    const std::vector<InputBinding>& get_bindings() const { return bindings; }
};

class InputBindingContext 
{
private:
    std::string context_name;
    std::unordered_map<std::string, InputAction> actions;
    int ctx_priority_;
    bool is_active_ctx_;

public:
    InputBindingContext(const std::string& ctx_name, int in_priority = 0)
        : context_name(ctx_name), ctx_priority_(in_priority), is_active_ctx_(false) {}
    
    InputAction& create_action(const std::string& _action_name)
    {
        auto it = actions.find(_action_name);
        if (it == actions.end()) 
        {
            actions.emplace(_action_name, InputAction(_action_name));
            return actions[_action_name];
        }
        return it->second;
    }

    InputAction* get_action(const std::string& _action_name) 
    {
        auto it = actions.find(_action_name);
        if (it != actions.end()) 
        {
            return &it->second;
        }
        return nullptr;
    }

    void remove_action(const std::string& _action_name)
    {
        auto it = actions.find(_action_name);
        if (it != actions.end()) 
        {
            actions.erase(it);
        }
    }

    void set_active(bool active) { is_active_ctx_ = active; }
    bool is_active() const { return is_active_ctx_; }
    int get_priority() const { return ctx_priority_; }
    const std::string& get_name() const { return context_name; }

    std::unordered_map<std::string, InputAction>& get_actions() { return actions; }
};

} // namespace toy3d