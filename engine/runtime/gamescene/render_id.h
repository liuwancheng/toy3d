#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace toy3d
{
    template<typename Tag>
    class RenderId
    {
    public:
        using ValueType = std::uint64_t;

        constexpr RenderId() = default;
        explicit constexpr RenderId(ValueType value) : value_(value) {}

        constexpr ValueType value() const { return value_; }
        explicit constexpr operator bool() const { return value_ != 0; }

        friend constexpr bool operator==(RenderId lhs, RenderId rhs)
        {
            return lhs.value_ == rhs.value_;
        }

        friend constexpr bool operator!=(RenderId lhs, RenderId rhs)
        {
            return !(lhs == rhs);
        }

    private:
        ValueType value_ = 0;
    };

    struct PrimitiveIdTag;
    struct LightIdTag;
    struct RenderSceneIdTag;
    struct ViewportIdTag;
    struct SceneOutputIdTag;
    struct MeshRenderResourceIdTag;
    struct MaterialRenderResourceIdTag;
    struct TextureRenderResourceIdTag;

    using PrimitiveId = RenderId<PrimitiveIdTag>;
    using LightId = RenderId<LightIdTag>;
    using RenderSceneId = RenderId<RenderSceneIdTag>;
    using ViewportId = RenderId<ViewportIdTag>;
    using SceneOutputId = RenderId<SceneOutputIdTag>;
    using MeshRenderResourceId = RenderId<MeshRenderResourceIdTag>;
    using MaterialRenderResourceId = RenderId<MaterialRenderResourceIdTag>;
    using TextureRenderResourceId = RenderId<TextureRenderResourceIdTag>;

    template<typename Id>
    Id allocate_render_id()
    {
        static_assert(std::is_same<typename Id::ValueType, std::uint64_t>::value,
            "allocate_render_id requires a Toy3d RenderId type");

        static std::atomic<std::uint64_t> next_value{1};
        const std::uint64_t value = next_value.fetch_add(1, std::memory_order_relaxed);
        return value == 0 ? Id{} : Id(value);
    }
}
