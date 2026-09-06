#pragma once

#include "rendercore/shader/shader_map_program.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    class GlobalShaderBindingRequirement final
    {
    public:
        GlobalShaderBindingRequirement(
            ShaderParameterId parameter_id,
            RHIBindingGroup group,
            RHIResourceBindingType type,
            std::uint32_t array_count,
            RHIShaderStageFlags stages);

        ShaderParameterId parameter_id() const { return parameter_id_; }
        RHIBindingGroup group() const { return group_; }
        RHIResourceBindingType type() const { return type_; }
        std::uint32_t array_count() const { return array_count_; }
        RHIShaderStageFlags stages() const { return stages_; }

        bool operator==(const GlobalShaderBindingRequirement& other) const;

    private:
        ShaderParameterId parameter_id_ = 0;
        RHIBindingGroup group_ = RHIBindingGroup::Material;
        RHIResourceBindingType type_ = RHIResourceBindingType::UniformBuffer;
        std::uint32_t array_count_ = 1;
        RHIShaderStageFlags stages_ = RHIShaderStageFlags::None;
    };

    class GlobalShaderType final
    {
    public:
        enum class ProgramKind
        {
            Graphics,
            Compute
        };

        GlobalShaderType(
            std::string type_name,
            std::string shader_name,
            std::string pass_name,
            ShaderContentHash permutation_key,
            ProgramKind program_kind,
            RHIShaderStageFlags required_stages,
            std::vector<GlobalShaderBindingRequirement> binding_requirements);

        const std::string& type_name() const { return type_name_; }
        const std::string& shader_name() const { return shader_name_; }
        const std::string& pass_name() const { return pass_name_; }
        const ShaderContentHash& permutation_key() const { return permutation_key_; }
        ProgramKind program_kind() const { return program_kind_; }
        RHIShaderStageFlags required_stages() const { return required_stages_; }
        const std::vector<GlobalShaderBindingRequirement>&
            binding_requirements() const
        {
            return binding_requirements_;
        }

        bool operator==(const GlobalShaderType& other) const;

    private:
        std::string type_name_;
        std::string shader_name_;
        std::string pass_name_;
        ShaderContentHash permutation_key_{};
        ProgramKind program_kind_ = ProgramKind::Graphics;
        RHIShaderStageFlags required_stages_ = RHIShaderStageFlags::None;
        std::vector<GlobalShaderBindingRequirement> binding_requirements_;
    };
}
