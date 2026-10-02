#pragma once

#include "misc/sha256.h"

namespace toy3d::shader
{
    // Geometry selects this engine dimension; influence width stays a draw value.
    enum class MeshVertexFactoryType
    {
        Local,
        GPUSkin
    };

    Sha256Hash mesh_shader_permutation_key(const Sha256Hash& material_key, MeshVertexFactoryType factory);
} // namespace toy3d::shader
