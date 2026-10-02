#include "renderscene/pass/mesh_draw_command.h"

#include <algorithm>

#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/shader_map.h"

namespace toy3d
{
    RHIStatus resolve_mesh_draw_binding(RHIDevice& device, const ShaderMapProgram& program, RHIBindingGroup group,
                                        const RHIBindingSetRef& owner_binding, RHIBindingSetRef& resolved_binding)
    {
        resolved_binding.reset();
        if (std::none_of(program.data().bindings.begin(), program.data().bindings.end(),
                         [group](const ShaderMapBinding& binding)
                         {
                             return binding.group == group;
                         }))
        {
            return RHIStatus::success();
        }
        if (!owner_binding)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady,
                                      "Program requires a logical binding that its owner did not provide.");
        }
        if (!owner_binding->is_owned_by(device) || owner_binding->group() != group)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Owner-provided logical binding has an incompatible device or group.");
        }
        resolved_binding = owner_binding;
        return RHIStatus::success();
    }
} // namespace toy3d
