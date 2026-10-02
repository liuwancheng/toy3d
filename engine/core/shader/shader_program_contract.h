#pragma once

#include <cstdint>
#include <string>

namespace toy3d::shader
{
    enum class ShaderStageFlags : std::uint8_t;
    enum class ShaderUsage : std::uint32_t
    {
        Global,
        Material,
        MeshPass
    };

    enum class ShaderPassRole : std::uint32_t
    {
        Global,
        Forward,
        ShadowDepth,
        HitProxy
    };

    enum class ShaderGeometryMode : std::uint32_t
    {
        None,
        Custom
    };

    enum class VertexFactoryType : std::uint32_t
    {
        None,
        Local,
        GPUSkin
    };

    constexpr std::uint32_t local_vertex_factory_support = 1u;
    constexpr std::uint32_t gpu_skin_vertex_factory_support = 2u;
    constexpr std::uint32_t all_vertex_factory_support = local_vertex_factory_support | gpu_skin_vertex_factory_support;

    // Shared authoring/artifact protocol; dynamic View/Primitive selection stays
    // in RenderScene. None explicitly identifies programs without mesh geometry.
    struct ShaderProgramContract
    {
        ShaderUsage usage = ShaderUsage::Global;
        ShaderPassRole role = ShaderPassRole::Global;
        ShaderGeometryMode geometry = ShaderGeometryMode::None;
        VertexFactoryType vertex_factory = VertexFactoryType::None;
        std::uint32_t vertex_factory_support = 0u;
    };

    bool supports_vertex_factory(std::uint32_t support, VertexFactoryType factory);
    bool validate_shader_program_contract(const ShaderProgramContract& contract, std::string& error);
    bool validate_shader_program_stages(const ShaderProgramContract& contract, ShaderStageFlags stages,
                                        std::string& error);
} // namespace toy3d::shader
