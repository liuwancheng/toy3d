#include "input_system.h"
#include "gamescene/camera/camera.h"
using namespace toy3d;

/////////////////////////////////////////////////////////////////////////////////////
/*  相机输入绑定示例代码
 *  这个函数设置了相机的输入绑定，允许用户使用键盘和鼠标控制相机移动和旋转
 */
void test_setup_camera_input(Camera& camera) 
{
    auto& input_sys = InputSystem::get_instance();
    
    // 创建并激活相机控制上下文
    auto& camera_ctx = input_sys.create_binding_context("CameraControls", 100);
    input_sys.activate_context(camera_ctx.get_name(), true);
    
    // 键盘WASD移动
    input_sys.bind_axis(camera_ctx.get_name(), "move_forward", KeyCode::W, 1.0f,
        [&camera](const InputEvent& event) {
            camera.move_back_forward(event.scale);
        });
        
    input_sys.bind_axis(camera_ctx.get_name(), "move_back", KeyCode::S, -1.0f,
        [&camera](const InputEvent& event) {
            camera.move_back_forward(event.scale);
        });
        
    input_sys.bind_axis(camera_ctx.get_name(), "move_right", KeyCode::D, 1.0f,
        [&camera](const InputEvent& event) {
            camera.move_right_left(event.scale);
        });
        
    input_sys.bind_axis(camera_ctx.get_name(), "move_left", KeyCode::A, -1.0f,
        [&camera](const InputEvent& event) {
            camera.move_right_left(event.scale);
        });
    
    // 空格和Ctrl上下移动
    input_sys.bind_action(camera_ctx.get_name(), "move_up", KeyCode::SPACE, KeyStatus::Hold,
        [&camera](const InputEvent& event) {
            camera.move_up_down(0.1f);
        });
        
    input_sys.bind_action(camera_ctx.get_name(), "move_down", KeyCode::CTRL, KeyStatus::Hold,
        [&camera](const InputEvent& event) {
            camera.move_up_down(-0.1f);
        });
    
    // 鼠标移动
    input_sys.bind_axis(camera_ctx.get_name(), "mouse_move", KeyCode::MOUSE_MOVE, 1.0f,
        [&camera](const InputEvent& event) {
            int dx, dy;
            InputSystem::get_instance().get_mouse_device()->get_mouse_delta(dx, dy);
            camera.rotate(dx * event.scale, dy * event.scale);
        });

}
