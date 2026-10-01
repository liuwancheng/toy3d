#pragma once

#include "math/angle.h"
#include "math/geometry/convex_volume.h"
#include "math/geometry/plane.h"
#include "math/integer_vector.h"
#include "math/length_units.h"
#include "math/matrix_construction.h"
#include "math/matrix3.h"
#include "math/matrix4.h"
#include "math/math_constants.h"
#include "math/quaternion.h"
#include "math/scalar_math.h"
#include "math/transform.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include "math/vector4.h"

#include "glm/glm.hpp"

namespace toy3d
{
    // GLM remains the public storage type only during the staged migration
    // described by document/math.md. New code must use Toy3d types.
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using uvec2 = glm::u32vec2;
    using uvec3 = glm::u32vec3;
    using uvec4 = glm::u32vec4;
    using mat3x3 = glm::mat3x3;
    using mat4x4 = glm::mat4x4;
    using mat3x4 = glm::mat3x4;
    using mat4x3 = glm::mat4x3;
    using quat = glm::quat;
    using color = glm::vec4;
} // namespace toy3d
