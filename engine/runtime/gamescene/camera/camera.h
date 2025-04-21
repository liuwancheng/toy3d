#pragma once

namespace toy3d
{

class Camera
{
public:
    Camera(){}
    ~Camera(){}
public:
    void move_back_forward(float t);

    void move_right_left(float t);

    void move_up_down(float t);

    void rotate(float x, float y);
private:
    float yaw;
    float pitch;
    float row; 
};

} // namespace toy3d