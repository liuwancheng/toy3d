#include "math/geometry/convex_volume.h"

namespace toy3d
{
    namespace
    {
        Vector4 matrix_row(const Matrix4& matrix, std::size_t row)
        {
            return Vector4(
                matrix.at(0, row),
                matrix.at(1, row),
                matrix.at(2, row),
                matrix.at(3, row));
        }
    }

    bool ConvexVolume::contains_point(const Vector3& point) const
    {
        if (!is_finite(point))
        {
            return false;
        }

        for (std::size_t index = 0; index < plane_count_; ++index)
        {
            if (planes_[index].signed_distance(point) < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    bool ConvexVolume::intersects_axis_aligned_bounds(
        const Vector3& minimum,
        const Vector3& maximum) const
    {
        if (!is_finite(minimum) ||
            !is_finite(maximum) ||
            minimum.x > maximum.x ||
            minimum.y > maximum.y ||
            minimum.z > maximum.z)
        {
            return false;
        }

        for (std::size_t index = 0; index < plane_count_; ++index)
        {
            const Vector3& normal = planes_[index].normal();
            const Vector3 positive_vertex(
                normal.x >= 0.0f ? maximum.x : minimum.x,
                normal.y >= 0.0f ? maximum.y : minimum.y,
                normal.z >= 0.0f ? maximum.z : minimum.z);
            if (planes_[index].signed_distance(positive_vertex) < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    bool try_make_reversed_z_frustum(
        const Matrix4& view_projection,
        bool infinite_far,
        ConvexVolume& result)
    {
        if (!is_finite(view_projection))
        {
            return false;
        }

        const Vector4 row0 = matrix_row(view_projection, 0);
        const Vector4 row1 = matrix_row(view_projection, 1);
        const Vector4 row2 = matrix_row(view_projection, 2);
        const Vector4 row3 = matrix_row(view_projection, 3);
        const std::array<Vector4, ConvexVolume::k_finite_frustum_plane_count>
            coefficients = {
                row3 + row0,
                row3 - row0,
                row3 + row1,
                row3 - row1,
                row3 - row2,
                row2};

        ConvexVolume volume;
        volume.plane_count_ = infinite_far ?
            ConvexVolume::k_infinite_frustum_plane_count :
            ConvexVolume::k_finite_frustum_plane_count;
        for (std::size_t index = 0; index < volume.plane_count_; ++index)
        {
            if (!try_make_plane(coefficients[index], volume.planes_[index]))
            {
                return false;
            }
        }

        result = volume;
        return true;
    }
}
