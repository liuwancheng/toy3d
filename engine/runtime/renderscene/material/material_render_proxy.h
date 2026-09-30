#pragma once

#include "drivers/rhi/rhi_device.h"
#include "math/math.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_parameters.h"
#include "material/material_asset_data.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace toy3d
{
    class Material;
    struct MaterialDesc;
    class RHICommandContext;
    class RenderResourceManager;
    class TextureResource;

    // MaterialInstance-owned stable Render-side representation. Its mutable
    // state is accessed only on the logical Rendering Thread.
    class MaterialRenderProxy final
    {
      public:
        explicit MaterialRenderProxy(const Material& material);
        explicit MaterialRenderProxy(const MaterialDesc& desc);

        RHIResult<RHIBindingSetRef> materialize(RHIDevice& device, RHICommandContext& context);
        RHIStatus begin_init_textures(RenderResourceManager& manager);

        RHIStatus stage_material_candidate(ShaderMapProgramRef shader_program, bool two_sided);
        RHIResult<RHIBindingSetRef> materialize_staged(RHIDevice& device, RHICommandContext& context);
        RHIStatus commit_material_candidate();
        void discard_material_candidate() noexcept;

        const ShaderMapProgramRef& shader_program() const noexcept { return shader_program_; }
        const shader::ShaderParameterSchema& parameter_schema() const noexcept { return parameter_schema_; }
        const shader::ShaderGraphicsPassState* effective_graphics_pass_state() const noexcept;

      private:
        friend class MaterialInstance;
        friend class MaterialInterface;
        void replace_state(MaterialRenderProxy&& candidate) noexcept;

        void apply_scalar_update(ShaderParameterId parameter_id, float value) noexcept;
        void apply_vector_update(ShaderParameterId parameter_id, const vec2& value) noexcept;
        void apply_vector_update(ShaderParameterId parameter_id, const vec3& value) noexcept;
        void apply_vector_update(ShaderParameterId parameter_id, const vec4& value) noexcept;
        void apply_texture_update(ShaderParameterId parameter_id, TextureResource* texture_resource) noexcept;

        RHIResult<RHIBindingSetRef> materialize_program(RHIDevice& device, RHICommandContext& context,
                                                        const ShaderMapProgramRef& shader_program, bool staged);
        bool texture_cache_matches(bool staged) const noexcept;
        bool texture_views_match(bool staged) const noexcept;

        std::string shader_name_;
        shader::ShaderParameterSchema parameter_schema_;
        ShaderParametersMetadata parameter_metadata_;
        ShaderMapProgramRef shader_program_;
        ShaderMapProgramRef staged_shader_program_;
        shader::ShaderGraphicsPassState effective_graphics_pass_state_;
        shader::ShaderGraphicsPassState staged_effective_graphics_pass_state_;
        std::unordered_map<ShaderParameterId, float> scalar_parameters_;
        std::unordered_map<ShaderParameterId, vec2> vector2_parameters_;
        std::unordered_map<ShaderParameterId, vec3> vector3_parameters_;
        std::unordered_map<ShaderParameterId, vec4> vector4_parameters_;
        std::unordered_map<ShaderParameterId, TextureResource*> texture_parameters_;
        std::unordered_map<ShaderParameterId, MaterialSamplerPreset> sampler_parameters_;
        std::unordered_map<MaterialSamplerPreset, RHISamplerRef> sampler_cache_;
        RHIBindingSetRef binding_set_;
        RHIBindingSetRef staged_binding_set_;
        std::unordered_map<TextureResource*, std::uint64_t> texture_generations_;
        std::unordered_map<TextureResource*, RHITextureViewRef> texture_views_;
        std::unordered_map<TextureResource*, std::uint64_t> staged_texture_generations_;
        std::unordered_map<TextureResource*, RHITextureViewRef> staged_texture_views_;
        RenderResourceManager* resource_manager_ = nullptr;
        bool dirty_ = true;
        bool staged_dirty_ = false;
        bool staged_materialized_ = false;
    };
} // namespace toy3d
