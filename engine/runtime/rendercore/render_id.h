#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
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

    // 这些空 Tag 只在编译期区分 ID 所属的领域，不会被实例化，也不占用运行时内存。
    // 因此 PrimitiveId 与 LightId 即使底层数值相同，仍是不能互相传递的不同 C++ 类型。
    struct PrimitiveIdTag;
    struct LightIdTag;
    struct RenderFrameIdTag;
    struct RenderSceneIdTag;
    struct ViewportIdTag;
    struct SceneOutputIdTag;
    struct MeshRenderResourceIdTag;
    struct MaterialRenderResourceIdTag;
    struct TextureRenderResourceIdTag;

    using PrimitiveId = RenderId<PrimitiveIdTag>;
    using LightId = RenderId<LightIdTag>;
    using RenderFrameId = RenderId<RenderFrameIdTag>;
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

        // 函数模板中的 static 变量按 Id 类型分别存在，使每类 RenderId 拥有独立的单调分配序列。
        static std::atomic<std::uint64_t> next_value{1};
        std::uint64_t value = next_value.load(std::memory_order_relaxed);
        while (value != 0)
        {
            const std::uint64_t following_value =
                value == std::numeric_limits<std::uint64_t>::max()
                    ? 0
                    : value + 1;
            if (next_value.compare_exchange_weak(
                value,
                following_value,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
            {
                return Id(value);
            }
        }
        return Id{};
    }
}
