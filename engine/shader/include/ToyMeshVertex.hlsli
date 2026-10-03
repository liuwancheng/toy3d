#ifndef TOY3D_MESH_VERTEX_INCLUDED
#define TOY3D_MESH_VERTEX_INCLUDED

#if TOY3D_GPU_SKIN
#include "/Engine/ShaderIncludes/ToyGPUSkin.hlsli"
#define TOY3D_SKIN_VERTEX_INPUT \
    uint4 bone_indices : BLENDINDICES0; \
    float4 bone_weights : BLENDWEIGHT0; \
    uint4 extra_bone_indices : BLENDINDICES1; \
    float4 extra_bone_weights : BLENDWEIGHT1;
#define TOY3D_DEFORM_VERTEX(input, out_position, out_normal) \
    toy_gpu_skin(toy_bone_matrices, toy_num_bone_influences, \
                 input.bone_indices, input.bone_weights, input.extra_bone_indices, input.extra_bone_weights, \
                 input.position.xyz, input.normal.xyz, out_position, out_normal)
#define TOY3D_DEFORM_FRAME(input, out_tangent, out_bitangent) \
    toy_gpu_skin_frame(toy_bone_matrices, toy_num_bone_influences, \
                       input.bone_indices, input.bone_weights, input.extra_bone_indices, input.extra_bone_weights, \
                       input.normal.xyz, input.tangent, out_tangent, out_bitangent)
#else
#define TOY3D_SKIN_VERTEX_INPUT
#define TOY3D_DEFORM_VERTEX(input, out_position, out_normal) \
    out_position = input.position.xyz; out_normal = input.normal.xyz
#define TOY3D_DEFORM_FRAME(input, out_tangent, out_bitangent) \
    out_tangent = input.tangent.xyz; out_bitangent = cross(input.normal.xyz, input.tangent.xyz) * input.tangent.w
#endif

#endif
