#include "rendercore/shader/global_shader_map.h"

#include <sstream>
#include <unordered_set>
#include <utility>

namespace toy3d
{
    GlobalShaderBindingRequirement::GlobalShaderBindingRequirement(ShaderParameterId parameter_id,
                                                                   RHIBindingGroup group, RHIResourceBindingType type,
                                                                   std::uint32_t array_count,
                                                                   RHIShaderStageFlags stages)
        : parameter_id_(parameter_id), group_(group), type_(type), array_count_(array_count), stages_(stages)
    {
    }

    bool GlobalShaderBindingRequirement::operator==(const GlobalShaderBindingRequirement& other) const
    {
        return parameter_id_ == other.parameter_id_ && group_ == other.group_ && type_ == other.type_ &&
               array_count_ == other.array_count_ && stages_ == other.stages_;
    }

    GlobalShaderType::GlobalShaderType(std::string type_name, std::string shader_name, std::string pass_name,
                                       ShaderContentHash permutation_key, ProgramKind program_kind,
                                       RHIShaderStageFlags required_stages,
                                       const ShaderParametersMetadata& parameter_metadata,
                                       std::vector<GlobalShaderBindingRequirement> binding_requirements)
        : type_name_(std::move(type_name)), shader_name_(std::move(shader_name)), pass_name_(std::move(pass_name)),
          permutation_key_(permutation_key), program_kind_(program_kind), required_stages_(required_stages),
          parameter_metadata_(parameter_metadata),
          binding_requirements_(std::move(binding_requirements))
    {
    }

    bool GlobalShaderType::operator==(const GlobalShaderType& other) const
    {
        return type_name_ == other.type_name_ && shader_name_ == other.shader_name_ && pass_name_ == other.pass_name_ &&
               permutation_key_ == other.permutation_key_ && program_kind_ == other.program_kind_ &&
               required_stages_ == other.required_stages_ &&
               parameter_metadata_.schema_identity == other.parameter_metadata_.schema_identity &&
               parameter_metadata_.group_identity == other.parameter_metadata_.group_identity &&
               binding_requirements_ == other.binding_requirements_;
    }

    namespace
    {
        std::string type_context(const GlobalShaderType& type)
        {
            return "Global Shader type '" + type.type_name() + "' [" + type.shader_name() + "/" + type.pass_name() +
                   "]";
        }

        RHIShaderStageFlags program_stages(const ShaderMapProgramData& program)
        {
            RHIShaderStageFlags result = RHIShaderStageFlags::None;
            for (const ShaderMapStage& stage : program.stages)
            {
                switch (stage.stage)
                {
                case RHIShaderStage::Vertex:
                    result |= RHIShaderStageFlags::Vertex;
                    break;
                case RHIShaderStage::Pixel:
                    result |= RHIShaderStageFlags::Pixel;
                    break;
                case RHIShaderStage::Geometry:
                    result |= RHIShaderStageFlags::Geometry;
                    break;
                case RHIShaderStage::Hull:
                    result |= RHIShaderStageFlags::Hull;
                    break;
                case RHIShaderStage::Domain:
                    result |= RHIShaderStageFlags::Domain;
                    break;
                case RHIShaderStage::Compute:
                    result |= RHIShaderStageFlags::Compute;
                    break;
                }
            }
            return result;
        }

        bool validate_type_descriptor(const GlobalShaderType& type, std::string& error)
        {
            if (type.type_name().empty() || type.shader_name().empty() || type.pass_name().empty() ||
                type.required_stages() == RHIShaderStageFlags::None)
            {
                error = type_context(type) + " has an incomplete immutable descriptor.";
                return false;
            }
            const RHIStatus metadata_status = validate_shader_parameters_metadata(type.parameter_metadata());
            if (!metadata_status)
            {
                error = type_context(type) + " has invalid generated parameters metadata: " +
                        metadata_status.message();
                return false;
            }
            const bool has_compute = EnumHasAnyFlags(type.required_stages(), RHIShaderStageFlags::Compute);
            const bool has_graphics = EnumHasAnyFlags(type.required_stages(), RHIShaderStageFlags::AllGraphics);
            if ((type.program_kind() == GlobalShaderType::ProgramKind::Graphics && (!has_graphics || has_compute)) ||
                (type.program_kind() == GlobalShaderType::ProgramKind::Compute && (!has_compute || has_graphics)))
            {
                error = type_context(type) + " has ProgramKind and required-stage mismatch.";
                return false;
            }

            std::unordered_set<ShaderParameterId> parameter_ids;
            for (const GlobalShaderBindingRequirement& requirement : type.binding_requirements())
            {
                if (requirement.parameter_id() == 0 || requirement.array_count() == 0 ||
                    requirement.stages() == RHIShaderStageFlags::None ||
                    !parameter_ids.insert(requirement.parameter_id()).second)
                {
                    error = type_context(type) + " has an invalid or duplicate binding requirement.";
                    return false;
                }
            }
            return true;
        }

        bool validate_program(const GlobalShaderType& type, ShaderPlatform platform,
                              const ShaderMapProgramData& program, std::string& error)
        {
            if (program.shader_name != type.shader_name() || program.pass_name != type.pass_name() ||
                program.permutation_key != type.permutation_key() || program.platform != platform)
            {
                error = type_context(type) + " loaded a Program with mismatched identity, permutation, or platform.";
                return false;
            }
            if (program_stages(program) != type.required_stages())
            {
                error = type_context(type) + " loaded a Program with mismatched exact stages.";
                return false;
            }
            const RHIStatus metadata_status = validate_shader_parameters_metadata_against_schema(
                type.parameter_metadata(), program.parameter_schema);
            if (!metadata_status)
            {
                error = type_context(type) + " generated parameters do not match the loaded Shader artifact: " +
                        metadata_status.message();
                return false;
            }
            for (const GlobalShaderBindingRequirement& requirement : type.binding_requirements())
            {
                const ShaderMapBinding* found = nullptr;
                for (const ShaderMapBinding& binding : program.bindings)
                {
                    if (binding.parameter_id == requirement.parameter_id())
                    {
                        found = &binding;
                        break;
                    }
                }
                if (found == nullptr || found->group != requirement.group() || found->type != requirement.type() ||
                    found->array_count != requirement.array_count() || found->stages != requirement.stages())
                {
                    std::ostringstream message;
                    message << type_context(type) << " binding requirement " << requirement.parameter_id()
                            << " does not match the loaded Program schema.";
                    error = message.str();
                    return false;
                }
            }
            return true;
        }
    } // namespace

    GlobalShaderMapResult GlobalShaderMap::load(ShaderMap& shader_map, ShaderPlatform platform,
                                                const std::vector<const GlobalShaderType*>& required_types)
    {
        GlobalShaderMap candidate(platform);
        for (const GlobalShaderType* type : required_types)
        {
            if (type == nullptr)
            {
                return {nullptr, "Global Shader load set contains a null type."};
            }
            std::string validation_error;
            if (!validate_type_descriptor(*type, validation_error))
            {
                return {nullptr, std::move(validation_error)};
            }
            if (candidate.programs_.find(type->type_name()) != candidate.programs_.end())
            {
                return {nullptr, "Duplicate Global Shader type name '" + type->type_name() + "'."};
            }

            ShaderMapProgramKey key;
            key.shader_name = type->shader_name();
            key.pass_name = type->pass_name();
            key.platform = platform;
            key.permutation_key = type->permutation_key();
            ShaderMapProgramResult loaded = shader_map.find_or_load(key);
            if (!loaded.succeeded())
            {
                return {nullptr, type_context(*type) + " failed to load: " + loaded.error};
            }
            if (!validate_program(*type, platform, loaded.program->data(), validation_error))
            {
                return {nullptr, std::move(validation_error)};
            }
            candidate.programs_.emplace(type->type_name(), Entry{*type, loaded.program});
        }
        return {std::make_shared<GlobalShaderMap>(std::move(candidate)), {}};
    }

    GlobalShaderMapResult GlobalShaderMap::replace(const std::vector<ShaderMapProgramRef>& programs) const
    {
        if (programs.size() != programs_.size())
            return {nullptr, "Global Shader replacement requires the complete frozen type set."};
        GlobalShaderMap candidate(platform_);
        for (const auto& program : programs)
        {
            if (!program) return {nullptr, "Global Shader replacement contains a null Program."};
            const Entry* matched = nullptr;
            for (const auto& entry : programs_)
                if (entry.second.type.shader_name() == program->data().shader_name &&
                    entry.second.type.pass_name() == program->data().pass_name) matched = &entry.second;
            if (!matched) return {nullptr, "Global Shader replacement contains an unregistered Program."};
            std::string error;
            if (!validate_program(matched->type, platform_, program->data(), error))
                return {nullptr, error + " Rebuild the application if generated parameters changed."};
            if (!candidate.programs_.emplace(matched->type.type_name(), Entry{matched->type, program}).second)
                return {nullptr, "Global Shader replacement contains duplicate Programs."};
        }
        return {std::make_shared<GlobalShaderMap>(std::move(candidate)), {}};
    }

    ShaderMapProgramResult GlobalShaderMap::find(const GlobalShaderType& type) const
    {
        const auto found = programs_.find(type.type_name());
        if (found == programs_.end())
        {
            return {nullptr, type_context(type) + " was not loaded into the frozen map."};
        }
        if (!(found->second.type == type))
        {
            return {nullptr, type_context(type) + " does not match the frozen type descriptor."};
        }
        return {found->second.program, {}};
    }
} // namespace toy3d
