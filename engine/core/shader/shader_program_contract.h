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
    constexpr std::uint32_t all_shader_pass_roles = 0x0fu;
    std::uint32_t shader_pass_role_bit(ShaderPassRole role);

    enum class ShaderGeometryMode : std::uint32_t
    {
        None,
        Custom,
        Standard
    };

    enum class VertexFactoryType : std::uint32_t
    {
        None,
        Local,
        GPUSkin
    };

    enum class ShaderSurfaceMode : std::uint32_t
    {
        Explicit,
        Opaque,
        Masked
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
        ShaderSurfaceMode surface_mode = ShaderSurfaceMode::Explicit;
    };

    bool supports_vertex_factory(std::uint32_t support, VertexFactoryType factory);
    bool valid_shader_source_name(const std::string& name);
    bool validate_shader_program_contract(const ShaderProgramContract& contract, std::string& error);
    bool validate_shader_program_stages(const ShaderProgramContract& contract, ShaderStageFlags stages,
                                        std::string& error);
} // namespace toy3d::shader
