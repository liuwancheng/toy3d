#ifndef TOY3D_GPU_SKIN_INCLUDED
#define TOY3D_GPU_SKIN_INCLUDED

// Six Float4 texels per draw-local bone: three affine position rows followed
// by three inverse-transpose normal rows. The Object schema owns the typed buffer.
void toy_gpu_skin_accumulate(Buffer<float4> bone_matrices, uint4 bone_indices, float4 bone_weights,
                            float4 position, float4 normal,
                            inout float3 skinned_position, inout float3 skinned_normal)
{
    [unroll]
    for (uint influence = 0; influence < 4; ++influence)
    {
        const uint first = bone_indices[influence] * 6;
        const float weight = bone_weights[influence];
        skinned_position += weight * float3(dot(bone_matrices.Load(first), position),
                                             dot(bone_matrices.Load(first + 1), position),
                                             dot(bone_matrices.Load(first + 2), position));
        skinned_normal += weight * float3(dot(bone_matrices.Load(first + 3), normal),
                                           dot(bone_matrices.Load(first + 4), normal),
                                           dot(bone_matrices.Load(first + 5), normal));
    }
}

void toy_gpu_skin(Buffer<float4> bone_matrices, uint num_bone_influences,
                  uint4 bone_indices, float4 bone_weights,
                  uint4 extra_bone_indices, float4 extra_bone_weights,
                  float3 bind_position, float3 bind_normal,
                  out float3 skinned_position, out float3 skinned_normal)
{
    skinned_position = float3(0, 0, 0);
    skinned_normal = float3(0, 0, 0);
    const float4 position = float4(bind_position, 1);
    const float4 normal = float4(bind_normal, 0);
    toy_gpu_skin_accumulate(bone_matrices, bone_indices, bone_weights,
                            position, normal, skinned_position, skinned_normal);
    // One value per draw, with the same bytecode and inputs for both storage widths.
    [branch]
    if (num_bone_influences == 8)
    {
        toy_gpu_skin_accumulate(bone_matrices, extra_bone_indices, extra_bone_weights,
                                position, normal, skinned_position, skinned_normal);
    }
    // Nonuniform LBS can cancel transformed normals. Keep this case finite.
    const float magnitude_squared = dot(skinned_normal, skinned_normal);
    const float reference_length_squared = dot(bind_normal, bind_normal);
    const float3 reference_normal = reference_length_squared > 1e-20
                                      ? bind_normal * rsqrt(reference_length_squared) : float3(0, 1, 0);
    skinned_normal = magnitude_squared > 1e-20 ? skinned_normal * rsqrt(magnitude_squared) : reference_normal;
}

#endif
