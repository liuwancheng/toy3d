#pragma once

#include "math/matrix4.h"
#include "math/vector3.h"

namespace toy3d
{
    // Render-side canonical values for the View logical Binding Group.
    // Shader layout metadata serializes these values later; this type is not a
    // native constant-buffer layout and must not be uploaded with raw memcpy.
    struct ViewUniformShaderParameters
    {
        Matrix4 view_matrix;
        Matrix4 projection_matrix;
        Matrix4 view_projection_matrix;
        Matrix4 inverse_view_matrix;
        Matrix4 inverse_projection_matrix;
        Matrix4 inverse_view_projection_matrix;
        Vector3 camera_position;
        float camera_position_padding = 0.0f;
        Vector3 camera_direction;
        float camera_direction_padding = 0.0f;
    };
}
