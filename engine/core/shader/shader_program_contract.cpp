#include "shader/shader_program_contract.h"

#include "shader/shader_format_types.h"

namespace toy3d::shader
{
    std::uint32_t shader_pass_role_bit(ShaderPassRole role)
    {
        switch (role)
        {
        case ShaderPassRole::Global:
            return 1u;
        case ShaderPassRole::Forward:
            return 2u;
        case ShaderPassRole::ShadowDepth:
            return 4u;
        case ShaderPassRole::HitProxy:
            return 8u;
        }
        return 0u;
    }
    bool valid_shader_source_name(const std::string& name)
    {
        if (name.empty() || name.size() > 256u || name.back() == '/')
        {
            return false;
        }
        bool segment_start = true;
        for (const char value : name)
        {
            if (value == '/')
            {
                if (segment_start)
                {
                    return false;
                }
                segment_start = true;
                continue;
            }
            const bool letter = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
            if (!letter && (segment_start || value < '0' || value > '9'))
            {
                return false;
            }
            segment_start = false;
        }
        return true;
    }

    bool supports_vertex_factory(std::uint32_t support, VertexFactoryType factory)
    {
        if ((support & ~all_vertex_factory_support) != 0u)
        {
            return false;
        }
        switch (factory)
        {
        case VertexFactoryType::None:
            return support == 0u;
        case VertexFactoryType::Local:
            return (support & local_vertex_factory_support) != 0u;
        case VertexFactoryType::GPUSkin:
            return (support & gpu_skin_vertex_factory_support) != 0u;
        }
        return false;
    }

    bool validate_shader_program_contract(const ShaderProgramContract& contract, std::string& error)
    {
        const bool global = contract.usage == ShaderUsage::Global;
        const bool material = contract.usage == ShaderUsage::Material;
        const bool mesh_pass = contract.usage == ShaderUsage::MeshPass;
        const bool mesh_role = contract.role == ShaderPassRole::Forward ||
                               contract.role == ShaderPassRole::ShadowDepth ||
                               contract.role == ShaderPassRole::HitProxy;
        const bool surface_valid =
            contract.geometry == ShaderGeometryMode::Standard
                ? (contract.usage == ShaderUsage::Material && (contract.surface_mode == ShaderSurfaceMode::Opaque ||
                                                               contract.surface_mode == ShaderSurfaceMode::Masked))
                : contract.surface_mode == ShaderSurfaceMode::Explicit;
        if ((!global && !material && !mesh_pass) ||
            (global && (contract.role != ShaderPassRole::Global || contract.geometry != ShaderGeometryMode::None ||
                        contract.vertex_factory != VertexFactoryType::None || contract.vertex_factory_support != 0u)) ||
            (!global &&
             (!mesh_role ||
              (contract.geometry != ShaderGeometryMode::Custom && contract.geometry != ShaderGeometryMode::Standard) ||
              contract.vertex_factory == VertexFactoryType::None)) ||
            (mesh_pass && contract.role == ShaderPassRole::Forward) ||
            !supports_vertex_factory(contract.vertex_factory_support, contract.vertex_factory) || !surface_valid)
        {
            error = "Invalid Shader usage/role/geometry/VertexFactory contract.";
            return false;
        }
        return true;
    }
    bool validate_shader_program_stages(const ShaderProgramContract& contract, ShaderStageFlags stages,
                                        std::string& error)
    {
        const ShaderStageFlags graphics = ShaderStageFlags::Vertex | ShaderStageFlags::Pixel;
        const bool graphics_program = stages == ShaderStageFlags::Vertex || stages == graphics;
        if (!validate_shader_program_contract(contract, error))
        {
            return false;
        }
        if ((!graphics_program && stages != ShaderStageFlags::Compute) ||
            (contract.usage != ShaderUsage::Global &&
             (!graphics_program || (contract.role != ShaderPassRole::ShadowDepth && stages != graphics))))
        {
            error = "Shader role requires a matching graphics/compute stage set; Forward and HitProxy require VS/PS.";
            return false;
        }
        return true;
    }
} // namespace toy3d::shader
