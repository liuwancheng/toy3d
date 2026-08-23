#pragma once

#include "drivers/rhi/rhi_device.h"
#include "math/math.h"
#include "rendercore/shader/shader_map.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace toy3d
{
    class Material;
    class TextureResource;

    // MaterialInstance-owned stable Render-side representation. Its mutable
    // state is accessed only on the logical Rendering Thread.
    class MaterialRenderProxy final
    {
    public:
        explicit MaterialRenderProxy(const Material& material);

        void set_scalar(ShaderParameterId parameter_id, float value) noexcept;
        void set_vector(ShaderParameterId parameter_id, const vec2& value) noexcept;
        void set_vector(ShaderParameterId parameter_id, const vec3& value) noexcept;
        void set_vector(ShaderParameterId parameter_id, const vec4& value) noexcept;
        void set_texture(
            ShaderParameterId parameter_id,
            TextureResource* texture_resource) noexcept;

        RHIResult<RHIBindingSetRef> materialize(
            RHIDevice& device,
            const RHIBindingLayoutRef& binding_layout);

        RHIStatus stage_material_candidate(
            ShaderMapProgramRef shader_program,
            bool two_sided);
        RHIResult<RHIBindingSetRef> materialize_staged(
            RHIDevice& device,
            const RHIBindingLayoutRef& binding_layout);
        RHIStatus commit_material_candidate();
        void discard_material_candidate() noexcept;

        const ShaderMapProgramRef& shader_program() const noexcept
        {
            return shader_program_;
        }
        const shader::ShaderGraphicsPassState* effective_graphics_pass_state()
            const noexcept;

    private:
        RHIResult<RHIBindingSetRef> materialize_program(
            RHIDevice& device,
            const RHIBindingLayoutRef& binding_layout,
            const ShaderMapProgramRef& shader_program,
            bool staged);
        bool texture_cache_matches(bool staged) const noexcept;
        bool texture_views_match(bool staged) const noexcept;

        std::string shader_name_;
        ShaderMapProgramRef shader_program_;
        ShaderMapProgramRef staged_shader_program_;
        shader::ShaderGraphicsPassState effective_graphics_pass_state_;
        shader::ShaderGraphicsPassState staged_effective_graphics_pass_state_;
        std::unordered_map<ShaderParameterId, float> scalar_parameters_;
        std::unordered_map<ShaderParameterId, vec2> vector2_parameters_;
        std::unordered_map<ShaderParameterId, vec3> vector3_parameters_;
        std::unordered_map<ShaderParameterId, vec4> vector4_parameters_;
        std::unordered_map<ShaderParameterId, TextureResource*> texture_parameters_;
        RHIBindingLayoutRef binding_layout_;
        RHIBindingSetRef binding_set_;
        RHIBindingLayoutRef staged_binding_layout_;
        RHIBindingSetRef staged_binding_set_;
        std::unordered_map<TextureResource*, std::uint64_t> texture_generations_;
        std::unordered_map<TextureResource*, RHITextureViewRef> texture_views_;
        std::unordered_map<TextureResource*, std::uint64_t> staged_texture_generations_;
        std::unordered_map<TextureResource*, RHITextureViewRef> staged_texture_views_;
        bool dirty_ = true;
        bool staged_dirty_ = false;
    };
}
