// Project includes use /Project/ShaderIncludes/project_common.hlsli.
// Keep shared author functions here; generated bindings remain compiler-owned.
float toy_project_stripe(float coordinate)
{
    return step(0.5, frac(coordinate));
}
