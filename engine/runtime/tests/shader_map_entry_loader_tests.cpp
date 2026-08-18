#include "rendercore/shader/loaders/shader_map_entry_loader.h"

#include "rendercore/shader/rhi_shader_program.h"
#include "rendercore/shader/shader_map.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    const toy3d::ShaderMapBinding* find_binding(
        const toy3d::ShaderMapProgramData& program,
        const std::string& name)
    {
        const auto found = std::find_if(program.bindings.begin(), program.bindings.end(),
            [&](const toy3d::ShaderMapBinding& binding) { return binding.name == name; });
        return found == program.bindings.end() ? nullptr : &*found;
    }

    void test_verified_entry_loads()
    {
        toy3d::ShaderMapEntryLoader loader(
            toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_TEST_ROOT));
        toy3d::ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Test/TestPass";
        key.pass_name = "TestPass";
        toy3d::ShaderMapProgramLoadResult loaded = loader.load_program(key);
        check(loaded.succeeded(), loaded.error.c_str());
        check(loaded.program->stages.size() == 2, "test Program must contain vertex and pixel stages");

        const toy3d::ShaderMapBinding* texture = find_binding(*loaded.program, "source_texture");
        const toy3d::ShaderMapBinding* sampler = find_binding(*loaded.program, "source_sampler");
        check(texture && sampler, "test Program must expose texture and sampler bindings");
        check(texture->group == toy3d::RHIBindingGroup::Material && texture->target_binding == 0,
            "texture must use VulkanPortable Material binding 0");
        check(sampler->group == toy3d::RHIBindingGroup::Material && sampler->target_binding == 1,
            "sampler must use VulkanPortable Material binding 1");

        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramResult mapped = shader_map.find_or_load(key);
        check(mapped.succeeded(), mapped.error.c_str());
        auto rhi_desc = toy3d::build_rhi_shader_program_desc(*mapped.program);
        check(rhi_desc.succeeded(), rhi_desc.status().message().c_str());
        check(rhi_desc.value().vertex_shader.has_value() &&
              rhi_desc.value().pixel_shader.has_value() &&
              !rhi_desc.value().compute_shader.has_value(),
            "RHI Program conversion must preserve the graphics stage set");
        check(rhi_desc.value().binding_layout.entries.size() == 2,
            "RHI binding layout must be generated from ShaderMap reflection");
        check(rhi_desc.value().pixel_shader->reflection.size() == 2,
            "pixel Shader reflection must be generated from the verified entry");

        toy3d::ShaderMapProgramData invalid = *loaded.program;
        invalid.stages.back().reflection.clear();
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "missing required stage reflection must fail runtime validation");

        invalid = *loaded.program;
        invalid.bindings.back().target_binding = invalid.bindings.front().target_binding;
        check(!toy3d::validate_shader_map_program(std::move(invalid), key).succeeded(),
            "duplicate Vulkan set/binding must fail runtime validation");
    }

    void test_target_and_identity_mismatch_fail()
    {
        toy3d::ShaderMapEntryLoader loader(
            toy3d::PhysicalPath(TOY3D_SHADER_MAP_ENTRY_TEST_ROOT));
        toy3d::ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Test/Missing";
        key.pass_name = "TestPass";
        check(!loader.load_program(key).succeeded(),
            "unknown Shader identity must fail diagnostically");

        key.shader_name = "Toy3d/Test/TestPass";
        key.platform = toy3d::ShaderPlatform::D3D11SM5;
        check(!loader.load_program(key).succeeded(),
            "unsupported ShaderPlatform must fail diagnostically");
    }
}

int main()
{
    try
    {
        test_verified_entry_loads();
        test_target_and_identity_mismatch_fail();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
