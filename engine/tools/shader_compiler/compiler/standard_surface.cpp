#include "compiler/standard_surface.h"

#include <algorithm>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        const char* vertex_source = R"hlsl(
#include "/Engine/ShaderIncludes/ToySurface.hlsli"
#include "/Engine/ShaderIncludes/ToyMeshVertex.hlsli"
struct ToyStandardVertex
{
    float4 position : POSITION0;
    float4 normal : NORMAL0;
    float2 uv : TEXCOORD0;
#if TOY3D_STANDARD_TANGENT
    float4 tangent : TANGENT0;
#endif
#if TOY3D_VARIANT_USE_VERTEX_COLOR
    float4 color : COLOR0;
#endif
    TOY3D_SKIN_VERTEX_INPUT
};
ToySurfaceVaryings toy_standard_vs(ToyStandardVertex input)
{
    float3 position, normal;
    TOY3D_DEFORM_VERTEX(input, position, normal);
    ToySurfaceVaryings output;
    const float4 world = mul(toy_object_to_world, float4(position, 1));
    output.world_position = world.xyz;
    output.world_normal = mul((float3x3)toy_object_normal_to_world, normal);
    output.world_tangent = float4(1, 0, 0, 1);
#if TOY3D_STANDARD_TANGENT
    float3 tangent, bitangent;
    TOY3D_DEFORM_FRAME(input, tangent, bitangent);
    const float3 world_normal = toy_surface_normalize(output.world_normal, float3(0, 0, 1));
    const float3 world_tangent = mul((float3x3)toy_object_to_world, tangent);
    const float3 world_bitangent = mul((float3x3)toy_object_to_world, bitangent);
    output.world_tangent = toy_surface_tangent_frame(world_normal, world_tangent, world_bitangent, input.tangent.w);
#endif
    output.uv = input.uv;
    output.color = 1;
#if TOY3D_VARIANT_USE_VERTEX_COLOR
    output.color = input.color;
#endif
#if TOY3D_STANDARD_SHADOW
    output.clip_position = mul(shadow_world_to_clip, world);
    const float3 bias_world_normal = toy_surface_normalize(output.world_normal, float3(0, 0, 1));
    const float NoL = saturate(abs(dot(bias_world_normal, shadow_light_direction.xyz)));
    const float slope = min(sqrt(max(0, 1 - NoL * NoL)) / max(NoL, 1.0e-4), shadow_bias_parameters.z);
    const float bias = min(shadow_bias_parameters.x + shadow_bias_parameters.y * slope, shadow_bias_parameters.w);
    output.clip_position.z = max(0, output.clip_position.z - bias * output.clip_position.w);
#else
    output.clip_position = mul(toy_view_projection, world);
#endif
    return output;
}
)hlsl";

        ShaderPass wrap_pass(const ShaderPass& source, ShaderPassRole role, bool masked, bool tangent_input)
        {
            ShaderPass pass = source;
            pass.role = role;
            pass.name = role == ShaderPassRole::Forward
                            ? source.name
                            : source.name + (role == ShaderPassRole::ShadowDepth ? "/ShadowDepth" : "/HitProxy");
            const auto& user_pixel = source.programs.front();
            const std::string shade = user_pixel.entry_points.front().name;
            HlslBlock vertex;
            vertex.location = source.location;
            vertex.source = std::string("#define TOY3D_STANDARD_SHADOW ") +
                            (role == ShaderPassRole::ShadowDepth ? "1\n" : "0\n") + "#define TOY3D_STANDARD_TANGENT " +
                            (tangent_input ? "1\n" : "0\n") + vertex_source;
            vertex.entry_points.push_back({ShaderStage::Vertex, "toy_standard_vs", source.location});
            HlslBlock pixel = user_pixel;
            pixel.source = "#include \"/Engine/ShaderIncludes/ToySurface.hlsli\"\n";
            if (role == ShaderPassRole::Forward)
            {
                pixel.source += user_pixel.source;
            }
            const bool shadow = role == ShaderPassRole::ShadowDepth;
            const bool hit = role == ShaderPassRole::HitProxy;
            pixel.source += std::string("\n") + (shadow ? "void" : (hit ? "uint" : "float4")) +
                            " toy_standard_ps(ToySurfaceVaryings varying, bool front_face : SV_IsFrontFace)" +
                            (shadow ? "" : " : SV_Target0") + "\n{\n";
            pixel.source += "    ToySurfaceInput input = toy_surface_input(varying, front_face);\n";
            if (masked)
            {
                pixel.source += "    clip(" + source.coverage_function + "(input));\n";
            }
            if (hit)
            {
                pixel.source += "    return uint(hit_proxy_id_parts.x) | (uint(hit_proxy_id_parts.y) << 16);\n";
            }
            else if (!shadow)
            {
                pixel.source += "    return " + shade + "(input);\n";
            }
            pixel.source += "}\n";
            pixel.entry_points = {{ShaderStage::Pixel, "toy_standard_ps", source.location}};
            pass.programs = {std::move(vertex), std::move(pixel)};
            if (role != ShaderPassRole::Forward)
            {
                pass.state = {};
                pass.state.cull_mode = source.state.cull_mode;
                pass.state.color_write_mask = shadow ? ShaderGraphicsPassState::ColorWriteMask::None
                                                     : ShaderGraphicsPassState::ColorWriteMask::Red;
            }
            return pass;
        }
    } // namespace

    bool expand_standard_surface(const ShaderAsset& source, const std::vector<ShaderVariantSelection>& selections,
                                 ShaderAsset& expanded, std::vector<Diagnostic>& diagnostics)
    {
        expanded = source;
        if (source.geometry != ShaderGeometryMode::Standard)
        {
            return true;
        }
        const auto permutation = resolve_shader_permutation(source, selections);
        if (!permutation.succeeded())
        {
            diagnostics = permutation.diagnostics;
            return false;
        }
        const auto mode = std::find_if(source.variants.begin(), source.variants.end(),
                                       [](const Variant& variant)
                                       {
                                           return variant.name == "SURFACE_MODE";
                                       });
        bool masked = false;
        if (mode != source.variants.end())
        {
            if (mode->type != VariantType::Enumeration || mode->options.size() != 2u ||
                std::find(mode->options.begin(), mode->options.end(), "Opaque") == mode->options.end() ||
                std::find(mode->options.begin(), mode->options.end(), "Masked") == mode->options.end())
            {
                diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidVariant, mode->location,
                                       "Standard SURFACE_MODE requires exactly Opaque and Masked."});
                return false;
            }
            const auto selected = std::find_if(selections.begin(), selections.end(),
                                               [](const ShaderVariantSelection& item)
                                               {
                                                   return item.name == "SURFACE_MODE";
                                               });
            masked = (selected == selections.end() ? mode->default_value : selected->value) == "Masked";
        }
        if (source.usage != ShaderUsage::Material || source.passes.size() != 1u ||
            source.passes.front().role != ShaderPassRole::Forward || source.passes.front().programs.size() != 1u ||
            source.passes.front().programs.front().entry_points.size() != 1u ||
            source.passes.front().programs.front().entry_points.front().stage != ShaderStage::Pixel ||
            (masked && source.passes.front().coverage_function.empty()))
        {
            diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::InvalidCompileRequest, source.location,
                 "Standard requires one Forward shading function; Masked requires CoverageFunction."});
            return false;
        }
        expanded.passes.clear();
        expanded.passes.push_back(
            wrap_pass(source.passes.front(), ShaderPassRole::Forward, masked, source.standard_tangent_input));
        if (masked)
        {
            expanded.passes.push_back(
                wrap_pass(source.passes.front(), ShaderPassRole::ShadowDepth, true, source.standard_tangent_input));
            expanded.passes.push_back(
                wrap_pass(source.passes.front(), ShaderPassRole::HitProxy, true, source.standard_tangent_input));
        }
        if (std::none_of(source.variants.begin(), source.variants.end(),
                         [](const Variant& variant)
                         {
                             return variant.name == "USE_VERTEX_COLOR";
                         }))
        {
            for (auto& pass : expanded.passes)
            {
                auto& vertex = pass.programs.front().source;
                const std::string name = "TOY3D_VARIANT_USE_VERTEX_COLOR";
                for (auto position = vertex.find(name); position != std::string::npos; position = vertex.find(name))
                {
                    vertex.replace(position, name.size(), "0");
                }
            }
        }
        return true;
    }

    ShaderPass standard_surface_probe(const ShaderAsset& source, bool coverage)
    {
        ShaderPass user = source.passes.front();
        if (coverage)
        {
            user.programs.front().source.clear();
        }
        ShaderPass pass = wrap_pass(user, ShaderPassRole::Forward, false, source.standard_tangent_input);
        if (coverage)
        {
            auto& pixel = pass.programs.back();
            const auto body = pixel.source.rfind("    return ");
            pixel.source.replace(body, pixel.source.size() - body,
                                 "    return float4(" + source.passes.front().coverage_function +
                                     "(input), 0, 0, 1);\n}\n");
        }
        return pass;
    }
} // namespace toy3d::shader
