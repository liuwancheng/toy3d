#pragma once

#include "math/geometry/plane.h"
#include "math/matrix4.h"

#include <array>
#include <cstddef>

namespace toy3d
{
    class ConvexVolume
    {
    public:
        constexpr ConvexVolume() = default;

        constexpr std::size_t plane_count() const
        {
            return plane_count_;
        }

        bool contains_point(const Vector3& point) const;
        bool intersects_axis_aligned_bounds(
            const Vector3& minimum,
            const Vector3& maximum) const;

    private:
        friend bool try_make_reversed_z_frustum(
            const Matrix4& view_projection,
            bool infinite_far,
            ConvexVolume& result);

        // C++17 inline constexpr members let the named contract dimensions
        // drive storage and loops without separate definitions.
        static constexpr std::size_t k_finite_frustum_plane_count = 6;
        static constexpr std::size_t k_infinite_frustum_plane_count = 5;

        std::array<Plane, k_finite_frustum_plane_count> planes_;
        std::size_t plane_count_ = 0;
    };

    bool try_make_reversed_z_frustum(
        const Matrix4& view_projection,
        bool infinite_far,
        ConvexVolume& result);
}
