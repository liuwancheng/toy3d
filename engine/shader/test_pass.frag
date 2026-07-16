#version 450

layout(set = 3, binding = 256) uniform texture2D source_texture;
layout(set = 3, binding = 512) uniform sampler source_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(sampler2D(source_texture, source_sampler), in_uv);
}
