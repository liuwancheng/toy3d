#include "config/render_backend_shader_platform.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/ui/imgui_renderer.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::ShaderMapProgramData make_program(const std::string& shader_name, const std::string& pass_name,
                                             toy3d::ShaderPlatform platform = toy3d::ShaderPlatform::VulkanES31)
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = shader_name;
        program.pass_name = pass_name;
        program.platform = platform;
        program.mapping_version = 1;
        program.logical_layout_hash[0] = 1;
        program.target_binding_hash[0] = 2;
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = toy3d::shader::default_shader_permutation_key;

        toy3d::ShaderMapBinding binding;
        binding.parameter_id = 10;
        binding.name = "required_texture";
        binding.group = toy3d::RHIBindingGroup::Pass;
        binding.type = toy3d::RHIResourceBindingType::SampledTexture;
        binding.stages = toy3d::RHIShaderStageFlags::Pixel;
        program.bindings.push_back(binding);

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {1, 2, 3, 4};
        vertex.content_hash[0] = 3;
        program.stages.push_back(vertex);

        toy3d::ShaderMapStage pixel;
        pixel.stage = toy3d::RHIShaderStage::Pixel;
        pixel.entry_point = "ps_main";
        pixel.binary = {5, 6, 7, 8};
        pixel.content_hash[0] = 4;
        pixel.reflection.push_back(binding);
        program.stages.push_back(pixel);
        return program;
    }

    class CollectionLoader final : public toy3d::ShaderMapLoader
    {
      public:
        explicit CollectionLoader(std::vector<toy3d::ShaderMapProgramData> programs) : programs_(std::move(programs)) {}

        toy3d::ShaderMapProgramLoadResult load_program(const toy3d::ShaderMapProgramKey& key) const override
        {
            ++load_count;
            for (const toy3d::ShaderMapProgramData& program : programs_)
            {
                if (program.shader_name == key.shader_name && program.pass_name == key.pass_name)
                {
                    return {program, {}};
                }
            }
            return {{}, "Injected missing Global Shader Program."};
        }

        mutable std::uint32_t load_count = 0;

      private:
        std::vector<toy3d::ShaderMapProgramData> programs_;
    };

    toy3d::GlobalShaderType make_type(std::string type_name, std::string shader_name, std::string pass_name,
                                      toy3d::RHIShaderStageFlags stages = toy3d::RHIShaderStageFlags::Vertex |
                                                                          toy3d::RHIShaderStageFlags::Pixel,
                                      toy3d::ShaderParameterId parameter_id = 10)
    {
        std::vector<toy3d::GlobalShaderBindingRequirement> bindings;
        bindings.emplace_back(parameter_id, toy3d::RHIBindingGroup::Pass, toy3d::RHIResourceBindingType::SampledTexture,
                              1, toy3d::RHIShaderStageFlags::Pixel);
        return toy3d::GlobalShaderType(std::move(type_name), std::move(shader_name), std::move(pass_name),
                                       toy3d::shader::default_shader_permutation_key,
                                       toy3d::GlobalShaderType::ProgramKind::Graphics, stages, std::move(bindings));
    }

    toy3d::ShaderMapProgramData make_program_for_type(const toy3d::GlobalShaderType& type, std::uint8_t hash_seed)
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = type.shader_name();
        program.pass_name = type.pass_name();
        program.mapping_version = 1;
        program.logical_layout_hash[0] = hash_seed;
        program.target_binding_hash[0] = static_cast<std::uint8_t>(hash_seed + 1u);
        program.pass_template_hash =
            toy3d::shader::calculate_shader_graphics_pass_state_hash(program.graphics_pass_state);
        program.permutation_key = type.permutation_key();
        std::uint32_t target_binding = 0;
        for (const toy3d::GlobalShaderBindingRequirement& requirement : type.binding_requirements())
        {
            toy3d::ShaderMapBinding binding;
            binding.parameter_id = requirement.parameter_id();
            binding.name = "required_" + std::to_string(binding.parameter_id);
            binding.group = requirement.group();
            binding.type = requirement.type();
            binding.stages = requirement.stages();
            binding.target_binding = target_binding++;
            binding.array_count = requirement.array_count();
            if (binding.type == toy3d::RHIResourceBindingType::UniformBuffer)
            {
                binding.constant_buffer_size = 16;
                toy3d::ShaderMapBinding::ConstantMember member;
                member.parameter_id = binding.parameter_id + 1u;
                member.name = "required_member";
                member.type = toy3d::ShaderValueType::Float32;
                member.size = sizeof(float);
                binding.constant_members.push_back(member);
            }
            program.bindings.push_back(binding);
        }

        const auto append_stage = [&program, hash_seed](toy3d::RHIShaderStage stage,
                                                        toy3d::RHIShaderStageFlags stage_flag, const char* entry_point,
                                                        std::uint8_t offset)
        {
            toy3d::ShaderMapStage result;
            result.stage = stage;
            result.entry_point = entry_point;
            result.binary = {hash_seed, offset, 2, 3};
            result.content_hash[0] = static_cast<std::uint8_t>(hash_seed + offset);
            for (const toy3d::ShaderMapBinding& binding : program.bindings)
            {
                if (EnumHasAnyFlags(binding.stages, stage_flag))
                {
                    result.reflection.push_back(binding);
                }
            }
            program.stages.push_back(std::move(result));
        };
        if (EnumHasAnyFlags(type.required_stages(), toy3d::RHIShaderStageFlags::Vertex))
        {
            append_stage(toy3d::RHIShaderStage::Vertex, toy3d::RHIShaderStageFlags::Vertex, "vs_main", 2);
        }
        if (EnumHasAnyFlags(type.required_stages(), toy3d::RHIShaderStageFlags::Pixel))
        {
            append_stage(toy3d::RHIShaderStage::Pixel, toy3d::RHIShaderStageFlags::Pixel, "ps_main", 3);
        }
        return program;
    }
} // namespace

int main()
{
    static_assert(!std::is_default_constructible<toy3d::GlobalShaderType>::value,
                  "GlobalShaderType must always carry an explicit immutable contract");
    static_assert(!std::is_default_constructible<toy3d::GlobalShaderBindingRequirement>::value,
                  "GlobalShaderBindingRequirement must not manufacture an empty binding");

    toy3d::ShaderPlatform selected_platform = toy3d::ShaderPlatform::VulkanES31;
    std::string platform_error;
    check(toy3d::try_get_shader_platform_for_backend("Vulkan", selected_platform, platform_error) &&
              selected_platform == toy3d::ShaderPlatform::VulkanES31,
          "Vulkan backend configuration must select VulkanES31");
    check(toy3d::try_get_shader_platform_for_backend("D3D11", selected_platform, platform_error) &&
              selected_platform == toy3d::ShaderPlatform::D3D11SM5,
          "D3D11 backend configuration must select D3D11SM5");
    check(toy3d::try_get_shader_platform_for_backend("D3D12", selected_platform, platform_error) &&
              selected_platform == toy3d::ShaderPlatform::D3D12SM6,
          "D3D12 backend configuration must select D3D12SM6");
    check(!toy3d::try_get_shader_platform_for_backend("", selected_platform, platform_error) &&
              platform_error.find("No RHI backend") != std::string::npos,
          "a build without an enabled backend must fail diagnostically");
    check(!toy3d::try_get_shader_platform_for_backend("Metal", selected_platform, platform_error) &&
              platform_error.find("Metal") != std::string::npos,
          "an unmapped backend must fail without guessing a ShaderPlatform");

    const toy3d::GlobalShaderType type = make_type("TestGlobalShader", "Toy3d/Test/Global", "Main");
    const toy3d::GlobalShaderType equal_type = make_type("TestGlobalShader", "Toy3d/Test/Global", "Main");
    const toy3d::GlobalShaderType different_type = make_type("TestGlobalShader", "Toy3d/Test/Global", "Other");
    check(type == equal_type && !(type == different_type),
          "Global Shader descriptor equality must cover its stable contract");

    CollectionLoader loader({make_program("Toy3d/Test/Global", "Main")});
    toy3d::ShaderMap shader_map(loader);
    toy3d::GlobalShaderMapResult loaded =
        toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&type});
    check(loaded.succeeded() && loaded.shader_map->size() == 1,
          "a matching required set must freeze as one complete GlobalShaderMap");
    if (loaded.succeeded())
    {
        const toy3d::ShaderMapProgramResult found = loaded.shader_map->find(equal_type);
        check(found.succeeded() && found.program != nullptr, "frozen map must support typed immutable lookup");
        check(!loaded.shader_map->find(different_type).succeeded(),
              "same type name with a different descriptor must fail lookup");
    }

    toy3d::GlobalShaderMapResult loaded_again =
        toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&type});
    check(loaded_again.succeeded() && loader.load_count == 1,
          "GlobalShaderMap must reuse the process ShaderMap CPU cache");

    std::shared_ptr<const toy3d::GlobalShaderMap> descriptor_lifetime_map;
    {
        const toy3d::GlobalShaderType local_type = make_type("TestGlobalShader", "Toy3d/Test/Global", "Main");
        toy3d::GlobalShaderMapResult local_loaded =
            toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&local_type});
        descriptor_lifetime_map = std::move(local_loaded.shader_map);
    }
    check(descriptor_lifetime_map && descriptor_lifetime_map->find(equal_type).succeeded(),
          "frozen GlobalShaderMap must own type descriptors by value rather than retaining caller pointers");

    toy3d::GlobalShaderMapResult duplicate =
        toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&type, &equal_type});
    check(!duplicate.succeeded() && duplicate.shader_map == nullptr &&
              duplicate.error.find("Duplicate") != std::string::npos,
          "duplicate stable type names must reject the entire candidate");

    const toy3d::GlobalShaderType missing_type = make_type("MissingGlobalShader", "Toy3d/Test/Missing", "Main");
    toy3d::GlobalShaderMapResult missing =
        toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&type, &missing_type});
    check(!missing.succeeded() && missing.shader_map == nullptr &&
              missing.error.find("MissingGlobalShader") != std::string::npos,
          "one missing required type must publish no partial map");

    const toy3d::GlobalShaderType wrong_stages =
        make_type("WrongStages", "Toy3d/Test/Global", "Main", toy3d::RHIShaderStageFlags::Vertex);
    toy3d::GlobalShaderMapResult stage_mismatch =
        toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&wrong_stages});
    check(!stage_mismatch.succeeded() && stage_mismatch.error.find("exact stages") != std::string::npos,
          "required stage mismatch must reject the candidate");

    const toy3d::GlobalShaderType wrong_binding =
        make_type("WrongBinding", "Toy3d/Test/Global", "Main",
                  toy3d::RHIShaderStageFlags::Vertex | toy3d::RHIShaderStageFlags::Pixel, 999);
    toy3d::GlobalShaderMapResult binding_mismatch =
        toy3d::GlobalShaderMap::load(shader_map, toy3d::ShaderPlatform::VulkanES31, {&wrong_binding});
    check(!binding_mismatch.succeeded() && binding_mismatch.error.find("binding requirement") != std::string::npos,
          "required logical binding mismatch must reject the candidate");

    CollectionLoader platform_loader({make_program("Toy3d/Test/Platform", "Main", toy3d::ShaderPlatform::D3D11SM5)});
    toy3d::ShaderMap platform_shader_map(platform_loader);
    const toy3d::GlobalShaderType platform_type = make_type("PlatformGlobalShader", "Toy3d/Test/Platform", "Main");
    toy3d::GlobalShaderMapResult platform_mismatch =
        toy3d::GlobalShaderMap::load(platform_shader_map, toy3d::ShaderPlatform::VulkanES31, {&platform_type});
    check(!platform_mismatch.succeeded() && platform_mismatch.shader_map == nullptr,
          "a Program from another platform must never enter the frozen map");

    toy3d::GlobalShaderTypeRegistry duplicate_registry;
    const toy3d::GlobalShaderTypeRegistration first_duplicate_registration(duplicate_registry, type);
    const toy3d::GlobalShaderTypeRegistration second_duplicate_registration(duplicate_registry, equal_type);
    toy3d::GlobalShaderTypeRegistryResult duplicate_registration = duplicate_registry.freeze();
    check(!duplicate_registration.succeeded() && duplicate_registration.error.find("Duplicate") != std::string::npos,
          "duplicate automatic Global Shader registration must fail registry freeze");

    toy3d::GlobalShaderTypeRegistry late_registry;
    const toy3d::GlobalShaderTypeRegistration initial_registration(late_registry, type);
    check(late_registry.freeze().succeeded(), "a valid isolated Global Shader registry must freeze successfully");
    const toy3d::GlobalShaderTypeRegistration late_registration(late_registry, different_type);
    toy3d::GlobalShaderTypeRegistryResult late_registration_result = late_registry.freeze();
    check(!late_registration_result.succeeded() &&
              late_registration_result.error.find("after registry freeze") != std::string::npos,
          "Global Shader registration after registry freeze must be rejected diagnostically");

    toy3d::GlobalShaderTypeRegistryResult registered_types = toy3d::GlobalShaderTypeRegistry::get().freeze();
    bool found_registered_tonemap = false;
    bool found_registered_imgui = false;
    for (const toy3d::GlobalShaderType* registered_type : registered_types.types)
    {
        found_registered_tonemap |= registered_type == &toy3d::tonemap_global_shader_type();
        found_registered_imgui |= registered_type == &toy3d::imgui_global_shader_type();
    }
    check(registered_types.succeeded() && found_registered_tonemap && found_registered_imgui,
          "built-in Global Shader types must register automatically before Engine initialization");
    bool registration_order_is_stable = true;
    for (std::size_t index = 1; index < registered_types.types.size(); ++index)
    {
        registration_order_is_stable &=
            registered_types.types[index - 1]->type_name() < registered_types.types[index]->type_name();
    }
    check(registration_order_is_stable,
          "Global Shader registry snapshots must not depend on translation-unit initialization order");

    std::string requirement_error;
    toy3d::GlobalShaderRequirements disabled_requirements(registered_types.types);
    check(disabled_requirements.add(toy3d::tonemap_global_shader_type(), requirement_error) &&
              disabled_requirements.add(toy3d::tonemap_global_shader_type(), requirement_error) &&
              disabled_requirements.types().size() == 1 &&
              disabled_requirements.types()[0] == &toy3d::tonemap_global_shader_type(),
          "Global Shader requirements must select registered types idempotently");
    check(!disabled_requirements.add(type, requirement_error) &&
              requirement_error.find("not registered") != std::string::npos,
          "Global Shader requirements must reject descriptors outside the frozen registry");
    check(disabled_requirements.add(toy3d::tonemap_global_shader_type(), requirement_error) &&
              requirement_error.empty(),
          "a successful Global Shader requirement must clear a prior diagnostic");

    toy3d::GlobalShaderRequirements enabled_requirements(registered_types.types);
    check(enabled_requirements.add(toy3d::tonemap_global_shader_type(), requirement_error) &&
              enabled_requirements.add(toy3d::imgui_global_shader_type(), requirement_error) &&
              enabled_requirements.types().size() == 2 &&
              enabled_requirements.types()[0] == &toy3d::tonemap_global_shader_type() &&
              enabled_requirements.types()[1] == &toy3d::imgui_global_shader_type(),
          "enabled ImGui must be selected explicitly from registered Global Shader types");

    const std::vector<const toy3d::GlobalShaderType*>& without_imgui = disabled_requirements.types();
    const std::vector<const toy3d::GlobalShaderType*>& with_imgui = enabled_requirements.types();

    CollectionLoader disabled_loader({make_program_for_type(toy3d::tonemap_global_shader_type(), 30),
                                      make_program_for_type(toy3d::imgui_global_shader_type(), 40)});
    toy3d::ShaderMap disabled_shader_map(disabled_loader);
    toy3d::GlobalShaderMapResult disabled_map =
        toy3d::GlobalShaderMap::load(disabled_shader_map, toy3d::ShaderPlatform::VulkanES31, without_imgui);
    check(disabled_map.succeeded() && disabled_loader.load_count == 1, "disabled ImGui must not issue ShaderMap I/O");

    CollectionLoader enabled_missing_loader({make_program_for_type(toy3d::tonemap_global_shader_type(), 50)});
    toy3d::ShaderMap enabled_missing_shader_map(enabled_missing_loader);
    toy3d::GlobalShaderMapResult enabled_missing =
        toy3d::GlobalShaderMap::load(enabled_missing_shader_map, toy3d::ShaderPlatform::VulkanES31, with_imgui);
    check(!enabled_missing.succeeded() && enabled_missing.shader_map == nullptr &&
              enabled_missing.error.find("ImGuiGlobalShader") != std::string::npos,
          "enabled ImGui load failure must reject the whole required set");

    if (failure_count != 0)
    {
        return 1;
    }
    std::cout << "Global Shader Map tests passed\n";
    return 0;
}
