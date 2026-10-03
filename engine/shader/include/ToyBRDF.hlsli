#ifndef TOY3D_BRDF_INCLUDED
#define TOY3D_BRDF_INCLUDED

// UE Mobile low-cost contract: D_GGX_Mobile and Lazarov EnvBRDFApprox.
// Float arithmetic keeps the baseline independent of native FP16 arithmetic.
static const float toy_pi = 3.14159265358979323846;

float toy_mobile_roughness(float roughness)
{
    return max(saturate(roughness), 0.015625);
}

float toy_mobile_ggx(float roughness, float no_h)
{
    const float alpha = roughness * roughness;
    const float scaled_no_h = no_h * alpha;
    const float p = alpha / (1.0 - no_h * no_h + scaled_no_h * scaled_no_h);
    return min(p * p / toy_pi, 2048.0);
}

float3 toy_mobile_env_brdf(float3 f0, float roughness, float no_v)
{
    const float4 fit = roughness * float4(-1.0, -0.0275, -0.572, 0.022)
                       + float4(1.0, 0.0425, 1.04, -0.04);
    const float a004 = min(fit.x * fit.x, exp2(-9.28 * no_v)) * fit.x + fit.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + fit.zw;
    return f0 * ab.x + saturate(50.0 * f0.g) * ab.y;
}

#endif
