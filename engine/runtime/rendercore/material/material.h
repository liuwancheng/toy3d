#pragma once

#include "math/math.h"
#include "rendercore/shader/shader_map_collection.h"
#include "shader/shader_binding_identity.h"
#include "shader/shader_format_types.h"
#include "rendercore/texture/texture.h"
#include "asset/material/material_asset_data.h"

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

    shader::ShaderParameterSchema material_parameter_schema_from_shader_schema(
        const shader::ShaderParameterSchema& shader_schema);
    // The full schema remains authorable; runtime resources are required only
    // when a Program in this compiled configuration uses the parameter.
    bool material_parameter_is_active(const ShaderMapCollection& shader_map, ShaderParameterId parameter_id);

    struct MaterialDesc
    {
        std::string shader_name;
        shader::ShaderParameterSchema parameter_schema;
        ShaderMapCollectionRef shader_map;
        // Authored values for this node only. Shader defaults are resolved from
        // the selected source revision, never copied into inherited overrides.
        std::vector<MaterialStaticOption> static_options;
        std::unordered_map<ShaderParameterId, float> scalar_defaults;
        std::unordered_map<ShaderParameterId, vec2> vector2_defaults;
        std::unordered_map<ShaderParameterId, vec3> vector3_defaults;
        std::unordered_map<ShaderParameterId, vec4> vector4_defaults;
        std::unordered_map<ShaderParameterId, TextureRef> texture_defaults;
        std::unordered_map<ShaderParameterId, MaterialSamplerPreset> sampler_defaults;
        bool two_sided = false;
    };

    // Decodes the complete schema, including inactive constants. Failure keeps
    // every existing default intact; resources are resolved by the creator.
    bool initialize_material_constant_defaults(MaterialDesc& desc, std::string& error);

    // CPU admission for the existing mesh formats. An absent map represents an
    // authoring-only material; render preparation still requires a loaded map.
    bool validate_material_geometry(const MaterialDesc& desc, shader::VertexFactoryType factory, bool has_vertex_colors,
                                    bool has_valid_tangent_frame, std::string& error);

    bool validate_material_mesh_pass(const MaterialDesc& desc, shader::ShaderPassRole role,
                                     shader::VertexFactoryType factory, std::string& error);

    // C++17 variant owns the closed runtime value set; monostate removes the
    // local override and restores Parent/default without a nullable resource.
    using MaterialParameterValue =
        std::variant<std::monostate, float, Vector2, Vector3, Vector4, TextureRef, MaterialSamplerPreset>;
    struct MaterialParameterChange
    {
        std::string name;
        MaterialParameterValue value;
    };
    using MaterialParameterChanges = std::vector<MaterialParameterChange>;

    class Material;
    class MaterialInterface;
    class MaterialInstance;
    class MaterialLibrary;
    using MaterialRef = std::shared_ptr<const Material>;
    using MaterialInterfaceRef = std::shared_ptr<const MaterialInterface>;
    using MaterialInstanceRef = std::shared_ptr<MaterialInstance>;

    // Shared read interface. Configuration publication belongs to the GT owner;
    // the stable Proxy address is an opaque identity outside the RT.
    class MaterialInterface
    {
      public:
        virtual ~MaterialInterface();
        MaterialInterface(const MaterialInterface&) = delete;
        MaterialInterface& operator=(const MaterialInterface&) = delete;
        MaterialInterface(MaterialInterface&& other) noexcept;
        MaterialInterface& operator=(MaterialInterface&&) = delete;
        const MaterialDesc& desc() const
        {
            return desc_;
        }
        const shader::ShaderParameterSchema& parameter_schema() const
        {
            return desc_.parameter_schema;
        }
        virtual const Material& root_material() const = 0;
        virtual MaterialInterfaceRef parent() const
        {
            return {};
        }
        // C++17 string_view borrows an authoring name only for this GT query.
        bool parameter_value(std::string_view name, MaterialParameterValue& output) const;
        bool overrides_parameter(std::string_view name) const;
        std::vector<MaterialStaticOption> effective_static_options() const;
        MaterialRenderProxy* material_render_proxy() const noexcept;
        static void release(MaterialInterfaceRef& material);

      protected:
        explicit MaterialInterface(MaterialDesc desc);
        bool validate_parameters(const MaterialParameterChanges& changes) const;
        bool apply_parameters(const MaterialParameterChanges& changes);
        bool stage_material_replacement(ShaderMapCollectionRef shader_map, bool two_sided);
        bool publish_material_replacement();
        bool discard_material_replacement();
        void retire_proxy();

      private:
        friend class MaterialInstance;
        friend class MaterialLibrary;
        struct Configuration
        {
            MaterialInterface* target = nullptr;
            MaterialDesc descriptor;
            MaterialParameterChanges overrides;
            MaterialInterfaceRef parent;
            bool shader_configuration_selected = false;
        };
        struct PreparedConfiguration
        {
            Configuration configuration;
            MaterialRef root;
            MaterialRef previous_root;
            bool previously_used = false;
            std::size_t depth = 1u;
            MaterialDesc effective;
            std::vector<MaterialStaticOption> static_options;
            MaterialRenderProxy* destination = nullptr;
            std::shared_ptr<MaterialRenderProxy> proxy;
        };
        static bool prepare_configurations(std::vector<Configuration> configurations,
                                           const std::vector<ShaderMapCollectionRef>& shader_family,
                                           std::vector<PreparedConfiguration>& revisions,
                                           std::vector<MaterialInstanceRef>& owners);
        static bool publish_configurations(std::vector<Configuration> configurations,
                                           const std::vector<ShaderMapCollectionRef>& shader_family = {});
        bool publish_tree(MaterialDesc desc, MaterialParameterChanges overrides);
        bool resolve_material_replacement_publication();
        MaterialDesc desc_;
        MaterialParameterChanges local_overrides_;
        mutable std::vector<std::weak_ptr<MaterialInstance>> children_;
        ShaderMapCollectionRef shader_map_;
        ShaderMapCollectionRef pending_shader_map_;
        bool two_sided_ = false;
        bool pending_two_sided_ = false;
        std::shared_ptr<std::atomic<bool>> replacement_commit_complete_ = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> replacement_commit_succeeded_ = std::make_shared<std::atomic<bool>>(false);
        bool replacement_publication_pending_ = false;
        mutable std::unique_ptr<MaterialRenderProxy> material_render_proxy_;
        mutable bool render_proxy_used_ = false;
        mutable bool release_enqueued_ = false;
    };

    class Material final : public MaterialInterface
    {
      public:
        static std::shared_ptr<Material> create(MaterialDesc desc);
        Material(Material&&) noexcept = default;
        const Material& root_material() const override
        {
            return *this;
        }

      private:
        explicit Material(MaterialDesc desc);
    };

    class MaterialInstance final : public MaterialInterface
    {
      public:
        static MaterialInstanceRef create(MaterialInterfaceRef parent, ShaderMapCollectionRef configuration = {});
        static MaterialInstanceRef create(MaterialInterfaceRef parent, ShaderMapCollectionRef configuration,
                                          std::vector<MaterialStaticOption> static_options);
        static void release(MaterialInstanceRef& instance);
        MaterialInstance(MaterialInstance&& other) noexcept;
        const MaterialRef& material() const
        {
            return material_;
        }
        const Material& root_material() const override
        {
            return *material_;
        }
        MaterialInterfaceRef parent() const override
        {
            return parent_;
        }
        // string_view resolves low-frequency authoring names before RT admission.
        bool set_scalar(std::string_view parameter_name, float value);
        bool set_vector(std::string_view parameter_name, const vec2& value);
        bool set_vector(std::string_view parameter_name, const vec3& value);
        bool set_vector(std::string_view parameter_name, const vec4& value);
        bool set_texture(std::string_view parameter_name, TextureRef texture);
        bool set_sampler(std::string_view parameter_name, MaterialSamplerPreset preset);
        bool reset_parameter(std::string_view parameter_name);
        using MaterialInterface::validate_parameters;
        using MaterialInterface::apply_parameters;
        using MaterialInterface::stage_material_replacement;
        using MaterialInterface::publish_material_replacement;
        using MaterialInterface::discard_material_replacement;

      private:
        friend class MaterialLibrary;
        // The common publisher commits direct Parent and resolved root together.
        friend class MaterialInterface;
        explicit MaterialInstance(MaterialInterfaceRef parent, MaterialRef root);
        MaterialInterfaceRef parent_;
        MaterialRef material_;
    };
} // namespace toy3d
