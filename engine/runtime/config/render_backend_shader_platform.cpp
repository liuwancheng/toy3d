#include "config/render_backend_shader_platform.h"

namespace toy3d
{
    bool try_get_shader_platform_for_backend(
        const std::string& backend_name,
        ShaderPlatform& output,
        std::string& error)
    {
        if (backend_name == "Vulkan")
        {
            output = ShaderPlatform::VulkanES31;
            error.clear();
            return true;
        }
        if (backend_name == "D3D11")
        {
            output = ShaderPlatform::D3D11SM5;
            error.clear();
            return true;
        }
        if (backend_name == "D3D12")
        {
            output = ShaderPlatform::D3D12SM6;
            error.clear();
            return true;
        }

        error = backend_name.empty()
            ? "No RHI backend is enabled for built-in Shader selection."
            : "RHI backend '" + backend_name +
                "' has no configured ShaderPlatform mapping.";
        return false;
    }
}
