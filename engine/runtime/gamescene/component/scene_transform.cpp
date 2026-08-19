#include "gamescene/component/scene_transform.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"

namespace toy3d
{
    mat4x4 make_transform_matrix(const SceneTransform& transform)
    {
        return glm::translate(mat4x4(1.0f), transform.translation) *
            glm::mat4_cast(transform.rotation) *
            glm::scale(mat4x4(1.0f), transform.scale);
    }
}
