#include "shader/shader_program_contract.h"

#include "shader/shader_format_types.h"

namespace toy3d::shader
{
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
        if ((!global && !material && !mesh_pass) ||
            (global && (contract.role != ShaderPassRole::Global || contract.geometry != ShaderGeometryMode::None ||
                        contract.vertex_factory != VertexFactoryType::None || contract.vertex_factory_support != 0u)) ||
            (!global && (!mesh_role || contract.geometry != ShaderGeometryMode::Custom ||
                         contract.vertex_factory == VertexFactoryType::None)) ||
            (mesh_pass && contract.role == ShaderPassRole::Forward) ||
            !supports_vertex_factory(contract.vertex_factory_support, contract.vertex_factory))
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
