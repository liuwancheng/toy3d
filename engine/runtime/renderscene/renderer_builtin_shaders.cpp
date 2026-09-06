#include "renderscene/renderer_builtin_shaders.h"

#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/ui/imgui_renderer.h"

namespace toy3d
{
    std::vector<const GlobalShaderType*> required_renderer_global_shader_types(bool enable_imgui)
    {
        std::vector<const GlobalShaderType*> result;
        result.push_back(&tonemap_global_shader_type());
        if (enable_imgui)
        {
            result.push_back(&imgui_global_shader_type());
        }
        return result;
    }
} // namespace toy3d
