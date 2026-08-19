#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace toy3d
{
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
        ~MaterialInstance() = default;

        MaterialInstance(const MaterialInstance&) = delete;
        MaterialInstance& operator=(const MaterialInstance&) = delete;
        MaterialInstance(MaterialInstance&&) noexcept = default;
        MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

        const MaterialRef& material() const { return material_; }
        std::uint64_t revision() const { return revision_; }

    private:
        explicit MaterialInstance(MaterialRef material);

        MaterialRef material_;
        std::uint64_t revision_ = 1;
    };

    using MaterialInstanceRef = std::shared_ptr<MaterialInstance>;
}
