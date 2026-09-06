#include "drivers/rhi/rhi_command_context.h"

namespace toy3d
{
    RHIStatus RHIGraphicsCommandContext::bind_graphics_bindings(const RHIGraphicsBindings& bindings)
    {
        const RHIStatus validation = validate_graphics_bindings(bindings);
        if (!validation)
        {
            return validation;
        }
        return bind_graphics_bindings_impl(bindings);
    }
} // namespace toy3d
