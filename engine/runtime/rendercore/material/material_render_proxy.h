#pragma once

#include "drivers/rhi/rhi_device.h"
#include "math/math.h"
#include "rendercore/shader/shader_map_collection.h"
#include "rendercore/shader/shader_parameters.h"
#include "asset/material/material_asset_data.h"
#include "rendercore/render_resource.h"
#include "rendercore/texture/texture_resource.h"

#include <cstdint>
#include <memory>
#include <map>
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
        RHIResult<RHIBindingSetRef> materialize(RHIDevice& device, RHICommandContext& context,
                                                const ShaderMapProgram& program);
        RHIStatus begin_init_textures(RenderResourceManager& manager);

        RHIStatus stage_material_candidate(ShaderMapCollectionRef shader_map, bool two_sided);
        RHIResult<RHIBindingSetRef> materialize_staged(RHIDevice& device, RHICommandContext& context);
        RHIStatus commit_material_candidate();
        void discard_material_candidate() noexcept;

        const ShaderMapCollectionRef& shader_map() const noexcept
        {
            return shader_map_;
        }
        const shader::ShaderParameterSchema& parameter_schema() const noexcept
        {
            return parameter_schema_;
        }
        shader::ShaderGraphicsPassState effective_graphics_pass_state(const ShaderMapProgram& program) const noexcept;
        bool two_sided() const noexcept
        {
            return two_sided_;
        }

      private:
        friend class MaterialInstance;
        friend class MaterialInterface;
        void replace_state(MaterialRenderProxy&& candidate) noexcept;

        void apply_scalar_update(ShaderParameterId parameter_id, float value) noexcept;
        void apply_vector_update(ShaderParameterId parameter_id, const vec2& value) noexcept;
        void apply_vector_update(ShaderParameterId parameter_id, const vec3& value) noexcept;
        void apply_vector_update(ShaderParameterId parameter_id, const vec4& value) noexcept;
        void apply_texture_update(ShaderParameterId parameter_id, TextureResource* texture_resource) noexcept;

        struct MaterialBindingCache
        {
            ShaderParametersMetadata metadata;
            RHIBindingSetRef binding_set;
            std::unordered_map<TextureResource*, std::uint64_t> texture_generations;
            std::unordered_map<TextureResource*, RHITextureViewRef> texture_views;
            bool dirty = true;
        };
        RHIResult<RHIBindingSetRef> materialize_configuration(RHIDevice& device, RHICommandContext& context,
                                                              const ShaderMapCollectionRef& shader_map, bool staged);
        RHIResult<RHIBindingSetRef> materialize_program(RHIDevice& device, RHICommandContext& context,
                                                        const ShaderMapCollectionRef& shader_map,
                                                        const ShaderMapProgram& program, bool staged);
        RHIStatus retain_configuration_bindings(const ShaderMapCollectionRef& shader_map,
                                                std::map<Sha256Hash, MaterialBindingCache>& bindings) const;
        void invalidate_parameter(ShaderParameterId parameter_id, bool constant) noexcept;
        bool texture_cache_matches(const MaterialBindingCache& binding, bool check_generation) const noexcept;
        void release_inactive_textures() noexcept;

        std::string shader_name_;
        shader::ShaderParameterSchema parameter_schema_;
        ShaderParametersMetadata parameter_metadata_;
        ShaderMapCollectionRef shader_map_;
        ShaderMapCollectionRef staged_shader_map_;
        bool two_sided_ = false;
        bool staged_two_sided_ = false;
        std::unordered_map<ShaderParameterId, float> scalar_parameters_;
        std::unordered_map<ShaderParameterId, vec2> vector2_parameters_;
        std::unordered_map<ShaderParameterId, vec3> vector3_parameters_;
        std::unordered_map<ShaderParameterId, vec4> vector4_parameters_;
        std::unordered_map<ShaderParameterId, TextureResource*> texture_parameters_;
        // Owned transport keeps opaque Texture identities alive before RT admission.
        std::unordered_map<ShaderParameterId, std::shared_ptr<RenderResource>> texture_owners_;
        std::unordered_map<ShaderParameterId, RenderResourceRef<TextureResource>> texture_refs_;
        std::unordered_map<ShaderParameterId, MaterialSamplerPreset> sampler_parameters_;
        std::unordered_map<MaterialSamplerPreset, RHISamplerRef> sampler_cache_;
        // Active group identities share bindings across roles/factories with
        // equivalent declarations; values and actual texture views invalidate
        // only the affected snapshots. Old GPU uses keep their strong references.
        std::map<Sha256Hash, MaterialBindingCache> bindings_;
        std::map<Sha256Hash, MaterialBindingCache> staged_bindings_;
        RenderResourceManager* resource_manager_ = nullptr;
        bool staged_materialized_ = false;
    };
} // namespace toy3d
