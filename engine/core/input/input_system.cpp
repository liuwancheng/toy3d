#include "input_system.h"

namespace toy3d 
{

bool InputSystem::init() 
{
    keyboard_device = std::make_shared<KeyboardDevice>();
    mouse_device = std::make_shared<MouseDevice>();
    
    return true;
}

void InputSystem::update() 
{    
    // 更新设备状态
    keyboard_device->update();
    mouse_device->update();
    
    // 处理持续按住的按键(Hold)事件
    for (auto& context : active_mapping_contexts) 
    {
        if (!context->is_active()) continue;
        
        for (auto& [action_name, action] : context->get_actions()) 
        {
            for (auto& binding : action.get_bindings()) 
            {                
                // 处理按键保持按住(Hold)的情况
                if (binding.input_state == KeyStatus::Hold) 
                {
                    if (keyboard_device->is_key_hold(binding.key_code)) 
                    {
                        KeyEvent event;
                        event.key_code = binding.key_code;
                        event.scale = binding.scale;
                        binding.callback(event);
                    }
                }
                else if (binding.input_state == KeyStatus::DoubleClick) 
                {
                    // 处理双击事件
                    if (keyboard_device->is_key_double_click(binding.key_code)) 
                    {
                        KeyEvent event;
                        event.key_code = binding.key_code;
                        binding.callback(event);
                    }
                }
            }
        }
    }
}

void InputSystem::process_event(InputEvent& event) 
{
    for (auto& context : active_mapping_contexts) 
    {
        if (!context->is_active()) continue;
        
        for (auto& [action_name, action] : context->get_actions()) 
        {
            // 处理绑定事件
            for (auto& binding : action.get_bindings()) 
            {
                bool should_trigger = false;
                
                // 根据事件类型和触发条件判断是否应该触发回调
                if (event.type == InputEventType::KeyPressed && binding.input_state == KeyStatus::Pressed
                || event.type == InputEventType::KeyReleased && binding.input_state == KeyStatus::Released
                || event.type == InputEventType::MouseButtonPressed && binding.input_state == KeyStatus::Pressed
                || event.type == InputEventType::MouseButtonReleased && binding.input_state == KeyStatus::Released) 
                {
                    const KeyEvent& key_event = static_cast<const KeyEvent&>(event);
                    should_trigger = (key_event.key_code == binding.key_code);
                    if (should_trigger && binding.callback) 
                    {
                        binding.callback(event);
                    }
                }
                else if (event.type == InputEventType::MouseMove) 
                {
                    // 处理鼠标移动事件
                    const MouseMoveEvent& mouse_event = static_cast<const MouseMoveEvent&>(event);
                    if (binding.input_state == KeyStatus::Hold) 
                    {
                        event.scale = binding.scale;
                        binding.callback(event);
                    }
                }
                else if (event.type == InputEventType::MouseWheel) 
                {
                    // 处理鼠标滚动事件
                    const MouseWheelEvent& mouse_event = static_cast<const MouseWheelEvent&>(event);
                    if (binding.input_state == KeyStatus::Hold) 
                    {
                        event.scale = binding.scale;
                        binding.callback(event);
                    }
                }

            }
        }
    }
}

InputBindingContext& InputSystem::create_binding_context(const std::string& name, int priority)
{
    auto [iter, inserted] = binding_contexts.emplace(name, InputBindingContext(name, priority));
    return iter->second;
}

void InputSystem::remove_binding_context(const std::string& name)
{
    auto it = std::find_if(active_mapping_contexts.begin(), active_mapping_contexts.end(),
                           [&name](const InputBindingContext* context) {
                               return context->get_name() == name;
                           });
    
    if (it != active_mapping_contexts.end()) {
        active_mapping_contexts.erase(it);
    }
    
    binding_contexts.erase(name);
}

void InputSystem::activate_context(const std::string& name, bool activate)
{
    auto it = binding_contexts.find(name);
    if (it == binding_contexts.end()) return;
    
    it->second.set_active(activate);
    
    if (activate)
    {
        // 确保上下文只添加一次
        auto active_it = std::find_if(active_mapping_contexts.begin(), active_mapping_contexts.end(),
                                    [&name](const InputBindingContext* context) {
                                        return context->get_name() == name;
                                    });
        
        if (active_it == active_mapping_contexts.end()) {
            active_mapping_contexts.push_back(&it->second);
            sort_active_mapping_contexts();
        }
    }
    else
    {
        // 移除上下文
        auto active_it = std::find_if(active_mapping_contexts.begin(), active_mapping_contexts.end(),
                                    [&name](const InputBindingContext* context) {
                                        return context->get_name() == name;
                                    });
        
        if (active_it != active_mapping_contexts.end()) {
            active_mapping_contexts.erase(active_it);
        }
    }
}

void InputSystem::sort_active_mapping_contexts()
{
    // 按优先级排序，优先级高的在前面
    std::sort(active_mapping_contexts.begin(), active_mapping_contexts.end(),
              [](const InputBindingContext* a, const InputBindingContext* b) {
                  return a->get_priority() > b->get_priority();
              });
}

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

/////////////////////////////////////////////////////////////////////////////////////
/*  相机输入绑定示例代码
 *  这个函数设置了相机的输入绑定，允许用户使用键盘和鼠标控制相机移动和旋转
void SetupCameraInput(Camera& camera) 
{
    auto& inputSys = InputSystem::Get();
    
    // 创建并激活相机控制上下文
    auto& cameraContext = inputSys.create_binding_context("CameraControls", 100);
    inputSys.ActivateContext("CameraControls", true);
    
    // 键盘WASD移动
    inputSys.bind_axis("CameraControls", "MoveForward", KeyCode::W, 1.0f,
        [&camera](const InputEvent& event) {
            camera.MoveForward(event.scale);
        });
        
    inputSys.bind_axis("CameraControls", "MoveBackward", KeyCode::S, -1.0f,
        [&camera](const InputEvent& event) {
            camera.MoveForward(event.scale);
        });
        
    inputSys.bind_axis("CameraControls", "MoveRight", KeyCode::D, 1.0f,
        [&camera](const InputEvent& event) {
            camera.MoveRight(event.scale);
        });
        
    inputSys.bind_axis("CameraControls", "MoveLeft", KeyCode::A, -1.0f,
        [&camera](const InputEvent& event) {
            camera.MoveRight(event.scale);
        });
    
    // 空格和Ctrl上下移动
    inputSys.bind_action("CameraControls", "MoveUp", KeyCode::SPACE, KeyStatus::Hold,
        [&camera](const InputEvent& event) {
            camera.MoveUp(0.1f);
        });
        
    inputSys.bind_action("CameraControls", "MoveDown", KeyCode::CTRL, KeyStatus::Hold,
        [&camera](const InputEvent& event) {
            camera.MoveUp(-0.1f);
        });
    
    // 鼠标移动
    inputSys.bind_axis("CameraControls", "MouseMoveX", KeyCode::MOUSE_MOVE, 1.0f,
        [&camera](const InputEvent& event) {
            int dx, dy;
            InputSystem::get_instance().get_mouse_device()->get_mouse_delta(dx, dy);
            camera.Rotate(dx * event.scale, dy * event.scale);
        });

}
*/