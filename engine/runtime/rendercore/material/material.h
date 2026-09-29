#pragma once

#include "math/math.h"
#include "format/shader_binding_identity.h"
#include "format/shader_format_types.h"
#include "rendercore/texture/texture.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace toy3d
{
    class MaterialRenderProxy;
    class ShaderMapProgram;

    enum class MaterialShadingModel
    {
        Phong
    };

    enum class MaterialBlendMode
    {
        Opaque,
        Translucent
    };

    shader::ShaderParameterSchema material_parameter_schema_from_shader_schema(
        const shader::ShaderParameterSchema& shader_schema);

    struct MaterialDesc
    {
        std::string shader_name;
        shader::ShaderParameterSchema parameter_schema;
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

    // Decodes the complete schema, including inactive constants. Failure keeps
    // every existing default intact; resources are resolved by the creator.
    bool initialize_material_constant_defaults(MaterialDesc& desc, std::string& error);

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
        const shader::ShaderParameterSchema& parameter_schema() const { return desc_.parameter_schema; }

      private:
        explicit Material(MaterialDesc desc);

        MaterialDesc desc_;
    };

    using MaterialRef = std::shared_ptr<const Material>;

    // C++17 variant owns the closed runtime value set; monostate resets to
    // the immutable Shader default without a nullable resource convention.
    using MaterialParameterValue = std::variant<std::monostate, float, Vector2, Vector3, Vector4, TextureRef>;
    struct MaterialParameterChange
    {
        std::string name;
        MaterialParameterValue value;
    };
    using MaterialParameterChanges = std::vector<MaterialParameterChange>;

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

        // string_view accepts canonical schema names without forcing the low-frequency
        // GT edit boundary to allocate; the view is resolved before any RT command is built.
        bool set_scalar(std::string_view parameter_name, float value);
        bool set_vector(std::string_view parameter_name, const vec2& value);
        bool set_vector(std::string_view parameter_name, const vec3& value);
        bool set_vector(std::string_view parameter_name, const vec4& value);
        bool set_texture(std::string_view parameter_name, TextureRef texture);
        bool reset_parameter(std::string_view parameter_name);
        bool validate_parameters(const MaterialParameterChanges& changes) const;
        bool apply_parameters(const MaterialParameterChanges& changes);

        bool stage_material_replacement(std::shared_ptr<const ShaderMapProgram> shader_program, bool two_sided);
        bool publish_material_replacement();
        bool discard_material_replacement();

        // This is an opaque FIFO-protected identity on the Game side. Only the
        // logical Rendering Thread may dereference the returned pointer.
        MaterialRenderProxy* material_render_proxy() noexcept;

      private:
        explicit MaterialInstance(MaterialRef material);

        bool resolve_material_replacement_publication();

        MaterialRef material_;
        std::shared_ptr<const ShaderMapProgram> shader_program_;
        std::shared_ptr<const ShaderMapProgram> pending_shader_program_;
        bool two_sided_ = false;
        bool pending_two_sided_ = false;
        std::shared_ptr<std::atomic<bool>> replacement_commit_complete_ = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> replacement_commit_succeeded_ = std::make_shared<std::atomic<bool>>(false);
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
} // namespace toy3d
