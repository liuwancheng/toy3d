#include "drivers/rhi/rhi_command_descriptors.h"

#include <iostream>
#include <memory>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::RHIBindingLayoutRef make_layout(bool include_view)
    {
        toy3d::RHIBindingLayoutDesc desc;
        desc.entries.push_back({toy3d::RHIBindingGroup::Global, 0,
            toy3d::RHIResourceBindingType::UniformBuffer,
            toy3d::RHIShaderStageFlags::Vertex, 1});
        if (include_view)
        {
            desc.entries.push_back({toy3d::RHIBindingGroup::View, 1,
                toy3d::RHIResourceBindingType::UniformBuffer,
                toy3d::RHIShaderStageFlags::Vertex, 1});
        }
        return std::make_shared<toy3d::RHIBindingLayout>(std::move(desc));
    }

    toy3d::RHIBindingSetRef make_set(
        const toy3d::RHIBindingLayoutRef& layout,
        toy3d::RHIBindingGroup group)
    {
        toy3d::RHIBindingSetDesc desc;
        desc.layout = layout;
        desc.group = group;
        return std::make_shared<toy3d::RHIBindingSet>(std::move(desc));
    }
}

int main()
{
    toy3d::RHIBindingLayoutDesc cross_group_slots;
    cross_group_slots.entries.push_back({toy3d::RHIBindingGroup::Global, 0,
        toy3d::RHIResourceBindingType::SampledTexture,
        toy3d::RHIShaderStageFlags::Pixel, 1});
    cross_group_slots.entries.push_back({toy3d::RHIBindingGroup::Material, 0,
        toy3d::RHIResourceBindingType::SampledTexture,
        toy3d::RHIShaderStageFlags::Pixel, 1});
    check(static_cast<bool>(toy3d::validate_binding_layout_desc(cross_group_slots)),
        "different logical groups may reuse target slots in different native namespaces");

    toy3d::RHIBindingLayoutDesc same_group_overlap = cross_group_slots;
    same_group_overlap.entries.back().group = toy3d::RHIBindingGroup::Global;
    check(!toy3d::validate_binding_layout_desc(same_group_overlap),
        "one logical group must reject overlapping target slots");

    const toy3d::RHIBindingLayoutRef layout = make_layout(true);
    const toy3d::RHIBindingSetRef global = make_set(
        layout, toy3d::RHIBindingGroup::Global);
    const toy3d::RHIBindingSetRef view = make_set(
        layout, toy3d::RHIBindingGroup::View);

    toy3d::RHIGraphicsBindings valid;
    valid.global = global;
    valid.view = view;
    check(static_cast<bool>(toy3d::validate_graphics_bindings(valid)),
        "Global and View sets with one compatible layout must validate");

    toy3d::RHIGraphicsBindings wrong_field;
    wrong_field.view = global;
    check(!toy3d::validate_graphics_bindings(wrong_field),
        "a logical binding set in the wrong field must fail");

    toy3d::RHIGraphicsBindings incompatible;
    incompatible.global = global;
    incompatible.view = make_set(make_layout(false), toy3d::RHIBindingGroup::View);
    check(!toy3d::validate_graphics_bindings(incompatible),
        "binding sets with incompatible layouts must fail");

    toy3d::RHIGraphicsBindings empty;
    check(static_cast<bool>(toy3d::validate_graphics_bindings(empty)),
        "an empty binding snapshot must be valid before pipeline requirements are known");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI binding tests passed\n";
    return 0;
}
