#pragma once

#include "math/math.h"
#include "rendercore/shader/shader_parameter_id.h"
#include "rendercore/texture/texture.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace toy3d
{
    class MaterialRenderProxy;
    class ShaderMapProgram;
    enum class ShaderValueType;

    enum class MaterialShadingModel
    {
        Phong
    };

    enum class MaterialBlendMode
    {
        Opaque,
        Translucent
    };

    struct MaterialDesc
    {
        std::string shader_name;
        std::shared_ptr<const ShaderMapProgram> shader_program;
        std::unordered_map<ShaderParameterId, float> scalar_defaults;
        std::unordered_map<ShaderParameterId, vec2> vector2_defaults;
        std::unordered_map<ShaderParameterId, vec3> vector3_defaults;
        std::unordered_map<ShaderParameterId, vec4> vector4_defaults;
        std::unordered_map<ShaderParameterId, TextureRef> texture_defaults;
        MaterialShadingModel shading_model = MaterialShadingModel::Phong;
        MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
        bool two_sided = false;
    };

    class Material
    {
    public:
        static std::shared_ptr<const Material> create(MaterialDesc desc);
        ~Material() = default;

        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;
        Material(Material&&) noexcept = default;
        Material& operator=(Material&&) noexcept = default;

        const MaterialDesc& desc() const { return desc_; }

    private:
        explicit Material(MaterialDesc desc);

        MaterialDesc desc_;
    };

    using MaterialRef = std::shared_ptr<const Material>;

    class MaterialInstance
    {
    public:
        static std::shared_ptr<MaterialInstance> create(MaterialRef material);
        // The caller must hold the final MaterialInstance reference. A used
        // Render-side representation is destroyed by the accepted command.
        static void release(std::shared_ptr<MaterialInstance>& material_instance);
        ~MaterialInstance();

        MaterialInstance(const MaterialInstance&) = delete;
        MaterialInstance& operator=(const MaterialInstance&) = delete;
        MaterialInstance(MaterialInstance&& other) noexcept;
        MaterialInstance& operator=(MaterialInstance&&) noexcept = delete;

        const MaterialRef& material() const { return material_; }

        bool set_scalar(ShaderParameterId parameter_id, float value);
        bool set_vector(ShaderParameterId parameter_id, const vec2& value);
        bool set_vector(ShaderParameterId parameter_id, const vec3& value);
        bool set_vector(ShaderParameterId parameter_id, const vec4& value);
        bool set_texture(ShaderParameterId parameter_id, TextureRef texture);

        bool stage_material_replacement(
            std::shared_ptr<const ShaderMapProgram> shader_program,
            bool two_sided);
        bool publish_material_replacement();
        bool discard_material_replacement();

        // This is an opaque FIFO-protected identity on the Game side. Only the
        // logical Rendering Thread may dereference the returned pointer.
        MaterialRenderProxy* material_render_proxy() noexcept;

    private:
        explicit MaterialInstance(MaterialRef material);

        bool validate_constant_parameter(
            ShaderParameterId parameter_id,
            ShaderValueType expected_value_type) const;
        bool validate_texture_parameter(ShaderParameterId parameter_id) const;
        bool resolve_material_replacement_publication();

        MaterialRef material_;
        std::shared_ptr<const ShaderMapProgram> shader_program_;
        std::shared_ptr<const ShaderMapProgram> pending_shader_program_;
        bool two_sided_ = false;
        bool pending_two_sided_ = false;
        std::shared_ptr<std::atomic<bool>> replacement_commit_complete_ =
            std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> replacement_commit_succeeded_ =
            std::make_shared<std::atomic<bool>>(false);
        bool replacement_publication_pending_ = false;
        std::unordered_map<ShaderParameterId, float> scalar_overrides_;
        std::unordered_map<ShaderParameterId, vec2> vector2_overrides_;
        std::unordered_map<ShaderParameterId, vec3> vector3_overrides_;
        std::unordered_map<ShaderParameterId, vec4> vector4_overrides_;
        std::unordered_map<ShaderParameterId, TextureRef> texture_overrides_;
        std::unique_ptr<MaterialRenderProxy> material_render_proxy_;
        bool render_proxy_used_ = false;
        bool release_enqueued_ = false;
    };

    using MaterialInstanceRef = std::shared_ptr<MaterialInstance>;
}
