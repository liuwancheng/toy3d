#include "math/math.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    bool nearly_equal(float left, float right, float tolerance = 1.0e-5f)
    {
        return std::fabs(left - right) <= tolerance;
    }

    void check_matrix_contract()
    {
        toy3d::Matrix4 translation;
        translation.at(3, 0) = 3.0f;
        translation.at(3, 1) = 4.0f;
        translation.at(3, 2) = 5.0f;
        check(nearly_equal(translation.at(3, 0), 3.0f),
            "Matrix indexing must remain column then row");

        const toy3d::Vector4 transformed =
            translation * toy3d::Vector4(1.0f, 2.0f, 3.0f, 1.0f);
        check(nearly_equal(transformed.x, 4.0f) &&
                nearly_equal(transformed.y, 6.0f) &&
                nearly_equal(transformed.z, 8.0f),
            "Matrices must multiply column vectors");

        toy3d::Matrix4 child_local;
        child_local.at(3, 1) = 2.0f;
        const toy3d::Vector4 child_origin = translation * child_local *
            toy3d::Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        check(nearly_equal(child_origin.x, 3.0f) &&
                nearly_equal(child_origin.y, 6.0f),
            "Child world must equal parent world times child local");
    }

    void check_matrix_inverse_and_transform_contract()
    {
        const toy3d::Matrix3 scale3(
            toy3d::Vector3(2.0f, 0.0f, 0.0f),
            toy3d::Vector3(0.0f, 3.0f, 0.0f),
            toy3d::Vector3(0.0f, 0.0f, 4.0f));
        toy3d::Matrix3 inverse3;
        check(nearly_equal(toy3d::determinant(scale3), 24.0f) &&
                toy3d::try_inverse(scale3, inverse3) &&
                toy3d::is_nearly_equal(
                    inverse3 * scale3, toy3d::Matrix3::identity()),
            "Matrix3 determinant and checked inverse must preserve column-major values");

        toy3d::Matrix4 transform;
        transform.at(0, 0) = 2.0f;
        transform.at(1, 1) = 3.0f;
        transform.at(2, 2) = 4.0f;
        transform.at(3, 0) = 5.0f;
        transform.at(3, 1) = 6.0f;
        transform.at(3, 2) = 7.0f;

        const toy3d::Vector3 position =
            toy3d::transform_position(transform, toy3d::Vector3(1.0f));
        const toy3d::Vector3 vector =
            toy3d::transform_vector(transform, toy3d::Vector3(1.0f));
        check(position == toy3d::Vector3(7.0f, 9.0f, 11.0f) &&
                vector == toy3d::Vector3(2.0f, 3.0f, 4.0f),
            "Matrix4 position transforms must include translation while vectors exclude it");

        toy3d::Matrix4 inverse4;
        check(nearly_equal(toy3d::determinant(transform), 24.0f) &&
                toy3d::try_inverse(transform, inverse4) &&
                toy3d::is_nearly_equal(
                    inverse4 * transform, toy3d::Matrix4::identity(), 1.0e-5f),
            "Matrix4 checked inverse must round-trip an affine transform");

        toy3d::Vector3 normal_result(9.0f);
        toy3d::Vector3 expected_normal;
        check(toy3d::try_normalize(
                    toy3d::Vector3(3.0f, 2.0f, 0.0f), expected_normal) &&
                toy3d::try_transform_normal(
                    transform,
                    toy3d::Vector3(1.0f, 1.0f, 0.0f),
                    normal_result) &&
                toy3d::is_nearly_equal(normal_result, expected_normal),
            "Normal transforms must use normalized inverse-transpose semantics");

        const toy3d::Matrix4 singular = toy3d::Matrix4::zero();
        const toy3d::Matrix4 unchanged = transform;
        inverse4 = unchanged;
        normal_result = toy3d::Vector3(7.0f);
        check(!toy3d::try_inverse(singular, inverse4) &&
                inverse4 == unchanged &&
                !toy3d::try_transform_normal(
                    singular, toy3d::Vector3(1.0f), normal_result) &&
                normal_result == toy3d::Vector3(7.0f),
            "Singular matrix operations must fail without modifying output");
    }

    void check_view_contract()
    {
        const toy3d::Vector3 up(0.0f, 1.0f, 0.0f);
        const toy3d::Vector3 forward(0.0f, 0.0f, 1.0f);
        toy3d::Quaternion orientation;
        check(toy3d::try_make_rotation_from_forward_up(
                    forward * 4.0f, up * 2.0f, orientation) &&
                toy3d::is_nearly_equal(
                    toy3d::rotate_vector(
                        orientation, toy3d::Vector3(1.0f, 0.0f, 0.0f)),
                    toy3d::Vector3(1.0f, 0.0f, 0.0f)) &&
                toy3d::is_nearly_equal(
                    toy3d::rotate_vector(orientation, up), up) &&
                toy3d::is_nearly_equal(
                    toy3d::rotate_vector(orientation, forward), forward),
            "Forward/up rotation construction must produce canonical left-handed axes");

        const toy3d::Vector3 eye(3.0f, 2.0f, -5.0f);
        toy3d::Matrix4 view;
        check(toy3d::try_make_look_at_view_matrix(
                eye, eye + forward, up, view),
            "A finite non-degenerate LookAt view must build");
        const toy3d::Vector4 eye_in_view = view * toy3d::Vector4(eye, 1.0f);
        const toy3d::Vector4 forward_in_view =
            view * toy3d::Vector4(eye + forward, 1.0f);
        check(nearly_equal(eye_in_view.x, 0.0f) &&
                nearly_equal(eye_in_view.y, 0.0f) &&
                nearly_equal(eye_in_view.z, 0.0f) &&
                nearly_equal(forward_in_view.z, 1.0f),
            "View space must place the eye at the origin and look toward +Z");

        toy3d::Matrix4 orientation_view;
        check(toy3d::try_make_rotation_from_forward_up(
                    forward, up, orientation) &&
                toy3d::try_make_view_matrix(
                    eye, orientation, orientation_view) &&
                toy3d::is_nearly_equal(view, orientation_view),
            "Orientation and LookAt view construction must share one rotation contract");

        const float infinity = std::numeric_limits<float>::infinity();
        const toy3d::Matrix4 unchanged = view;
        toy3d::Matrix4 invalid_result = unchanged;
        check(!toy3d::try_make_look_at_view_matrix(
                    eye, eye, up, invalid_result) &&
                invalid_result == unchanged &&
                !toy3d::try_make_look_at_view_matrix(
                    eye, eye + forward, forward, invalid_result) &&
                invalid_result == unchanged &&
                !toy3d::try_make_view_matrix(
                    toy3d::Vector3(infinity, 0.0f, 0.0f),
                    orientation,
                    invalid_result) &&
                invalid_result == unchanged,
            "Degenerate and non-finite view construction must fail atomically");
    }

    void check_projection_contract()
    {
        const float near_clip = 1.0f;
        const float far_clip = 101.0f;
        toy3d::PerspectiveProjectionDesc desc;
        desc.vertical_fov = toy3d::Radians(toy3d::k_half_pi);
        desc.aspect = 2.0f;
        desc.near_clip = near_clip;
        desc.far_clip = far_clip;
        toy3d::Matrix4 projection;
        check(toy3d::try_make_perspective_projection(desc, projection),
            "A valid finite reversed-Z projection must build");
        check(nearly_equal(projection.at(0, 0), 0.5f) &&
                nearly_equal(projection.at(1, 1), 1.0f) &&
                nearly_equal(projection.at(2, 2), -0.01f) &&
                nearly_equal(projection.at(2, 3), 1.0f) &&
                nearly_equal(projection.at(3, 2), 1.01f),
            "Projection must retain CPU/HLSL column-vector golden values");

        const toy3d::Vector4 near_value =
            projection * toy3d::Vector4(0.0f, 0.0f, near_clip, 1.0f);
        const toy3d::Vector4 far_value =
            projection * toy3d::Vector4(0.0f, 0.0f, far_clip, 1.0f);
        check(nearly_equal(near_value.z / near_value.w, 1.0f) &&
                nearly_equal(far_value.z / far_value.w, 0.0f),
            "Reversed-Z must map near to 1 and finite far to 0");
        check(projection.at(1, 1) > 0.0f,
            "Projection must not contain a Vulkan-specific Y flip");

        toy3d::InfinitePerspectiveProjectionDesc infinite_desc;
        infinite_desc.vertical_fov = desc.vertical_fov;
        infinite_desc.aspect = desc.aspect;
        infinite_desc.near_clip = near_clip;
        toy3d::Matrix4 infinite_projection;
        check(toy3d::try_make_infinite_perspective_projection(
                    infinite_desc, infinite_projection),
            "A valid infinite reversed-Z projection must build");
        const toy3d::Vector4 infinite_near = infinite_projection *
            toy3d::Vector4(0.0f, 0.0f, near_clip, 1.0f);
        const toy3d::Vector4 infinite_far = infinite_projection *
            toy3d::Vector4(0.0f, 0.0f, 1000000.0f, 1.0f);
        check(nearly_equal(infinite_near.z / infinite_near.w, 1.0f) &&
                nearly_equal(infinite_far.z / infinite_far.w, 0.0f, 1.0e-5f),
            "Infinite reversed-Z must map near to 1 and approach 0 at distance");

        const toy3d::Matrix4 unchanged = projection;
        toy3d::Matrix4 invalid_result = unchanged;
        desc.aspect = 0.0f;
        check(!toy3d::try_make_perspective_projection(desc, invalid_result) &&
                invalid_result == unchanged,
            "Invalid projection input must fail without modifying output");
    }

    void check_plane_and_convex_volume_contract()
    {
        toy3d::Plane plane;
        check(toy3d::try_make_plane(
                    toy3d::Vector4(2.0f, 0.0f, 0.0f, -2.0f),
                    plane) &&
                toy3d::is_nearly_equal(
                    plane.normal(), toy3d::Vector3(1.0f, 0.0f, 0.0f)) &&
                nearly_equal(plane.signed_distance(
                    toy3d::Vector3(3.0f, 0.0f, 0.0f)), 2.0f),
            "Plane construction must normalize coefficients and preserve signed distance");

        const toy3d::Plane unchanged_plane = plane;
        const float infinity = std::numeric_limits<float>::infinity();
        check(!toy3d::try_make_plane(toy3d::Vector4(), plane) &&
                nearly_equal(plane.signed_distance(
                    toy3d::Vector3(3.0f, 0.0f, 0.0f)),
                    unchanged_plane.signed_distance(
                        toy3d::Vector3(3.0f, 0.0f, 0.0f))) &&
                !toy3d::try_make_plane(
                    toy3d::Vector4(infinity, 0.0f, 0.0f, 0.0f), plane) &&
                nearly_equal(plane.signed_distance(
                    toy3d::Vector3(3.0f, 0.0f, 0.0f)),
                    unchanged_plane.signed_distance(
                        toy3d::Vector3(3.0f, 0.0f, 0.0f))),
            "Degenerate and non-finite Plane construction must fail atomically");

        toy3d::PerspectiveProjectionDesc finite_desc;
        finite_desc.vertical_fov = toy3d::Radians(toy3d::k_half_pi);
        finite_desc.aspect = 1.0f;
        finite_desc.near_clip = 1.0f;
        finite_desc.far_clip = 101.0f;
        toy3d::Matrix4 finite_projection;
        toy3d::ConvexVolume finite_volume;
        check(toy3d::try_make_perspective_projection(
                    finite_desc, finite_projection) &&
                toy3d::try_make_reversed_z_frustum(
                    finite_projection, false, finite_volume) &&
                finite_volume.plane_count() == 6,
            "A finite reversed-Z frustum must contain six valid planes");
        check(finite_volume.contains_point(
                    toy3d::Vector3(0.0f, 0.0f, 2.0f)) &&
                !finite_volume.contains_point(
                    toy3d::Vector3(0.0f, 0.0f, 102.0f)) &&
                finite_volume.intersects_axis_aligned_bounds(
                    toy3d::Vector3(-0.25f, -0.25f, 1.5f),
                    toy3d::Vector3(0.25f, 0.25f, 2.5f)) &&
                !finite_volume.intersects_axis_aligned_bounds(
                    toy3d::Vector3(-4.0f, -0.25f, 1.5f),
                    toy3d::Vector3(-3.0f, 0.25f, 2.5f)) &&
                finite_volume.intersects_axis_aligned_bounds(
                    toy3d::Vector3(-2.5f, -0.25f, 1.5f),
                    toy3d::Vector3(-1.5f, 0.25f, 2.5f)) &&
                finite_volume.intersects_axis_aligned_bounds(
                    toy3d::Vector3(-0.25f, -0.25f, 0.5f),
                    toy3d::Vector3(0.25f, 0.25f, 1.0f)),
            "Frustum AABB tests must distinguish inside, outside, intersecting, and touching bounds");

        toy3d::InfinitePerspectiveProjectionDesc infinite_desc;
        infinite_desc.vertical_fov = finite_desc.vertical_fov;
        infinite_desc.aspect = finite_desc.aspect;
        infinite_desc.near_clip = finite_desc.near_clip;
        toy3d::Matrix4 infinite_projection;
        toy3d::ConvexVolume infinite_volume;
        check(toy3d::try_make_infinite_perspective_projection(
                    infinite_desc, infinite_projection) &&
                toy3d::try_make_reversed_z_frustum(
                    infinite_projection, true, infinite_volume) &&
                infinite_volume.plane_count() == 5 &&
                infinite_volume.contains_point(
                    toy3d::Vector3(0.0f, 0.0f, 1000000.0f)),
            "An infinite reversed-Z frustum must omit Far and retain five valid planes");

        const toy3d::ConvexVolume unchanged_volume = finite_volume;
        toy3d::Matrix4 non_finite_projection = finite_projection;
        non_finite_projection.at(0, 0) = infinity;
        check(!toy3d::try_make_reversed_z_frustum(
                    toy3d::Matrix4::zero(), false, finite_volume) &&
                finite_volume.plane_count() == unchanged_volume.plane_count() &&
                finite_volume.contains_point(
                    toy3d::Vector3(0.0f, 0.0f, 2.0f)) &&
                !toy3d::try_make_reversed_z_frustum(
                    non_finite_projection, false, finite_volume) &&
                finite_volume.plane_count() == unchanged_volume.plane_count() &&
                finite_volume.contains_point(
                    toy3d::Vector3(0.0f, 0.0f, 2.0f)),
            "Invalid frustum matrices must fail without modifying the output volume");
    }

    void check_scalar_contract()
    {
        check(toy3d::min(-2.0f, 3.0f) == -2.0f &&
                toy3d::max(-2.0f, 3.0f) == 3.0f &&
                toy3d::clamp(4.0f, -1.0f, 2.0f) == 2.0f &&
                toy3d::lerp(2.0f, 6.0f, 0.25f) == 3.0f &&
                toy3d::square(-3.0f) == 9.0f,
            "Scalar total operations must retain their named semantics");
        check(toy3d::is_nearly_zero(1.0e-6f) &&
                !toy3d::is_nearly_zero(1.1e-6f) &&
                toy3d::is_nearly_equal(1.0f, 1.0f + 1.0e-6f, 1.1e-6f),
            "Scalar comparisons must use an inclusive absolute tolerance");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const double double_infinity =
            std::numeric_limits<double>::infinity();
        check(!toy3d::is_finite(nan) &&
                !toy3d::is_finite(infinity) &&
                !toy3d::is_finite(double_infinity) &&
                toy3d::is_finite(1.0) &&
                !toy3d::is_nearly_zero(nan) &&
                !toy3d::is_nearly_equal(1.0f, 1.0f, -1.0f),
            "Scalar predicates must reject invalid float/double values and negative tolerance");
    }

    void check_angle_contract()
    {
        static_assert(!std::is_convertible<float, toy3d::Radians>::value,
            "A raw float must not convert implicitly to Radians.");
        static_assert(!std::is_convertible<float, toy3d::Degrees>::value,
            "A raw float must not convert implicitly to Degrees.");
        static_assert(sizeof(toy3d::Radians) == sizeof(float),
            "Radians must retain one-float storage.");
        static_assert(sizeof(toy3d::Degrees) == sizeof(float),
            "Degrees must retain one-float storage.");
        static_assert(std::is_standard_layout<toy3d::Radians>::value &&
                std::is_trivially_copyable<toy3d::Radians>::value,
            "Radians must be a simple value type.");
        static_assert(std::is_standard_layout<toy3d::Degrees>::value &&
                std::is_trivially_copyable<toy3d::Degrees>::value,
            "Degrees must be a simple value type.");

        const toy3d::Degrees half_turn(180.0f);
        const toy3d::Radians radians = toy3d::to_radians(half_turn);
        const toy3d::Degrees round_trip = toy3d::to_degrees(radians);
        check(nearly_equal(radians.value(), toy3d::k_pi) &&
                nearly_equal(round_trip.value(), half_turn.value()),
            "Degree and radian conversion must round-trip explicitly");

        const toy3d::Radians quarter_turn = radians * 0.5f;
        check(nearly_equal(quarter_turn.value(), toy3d::k_half_pi) &&
                nearly_equal(toy3d::sin(quarter_turn), 1.0f) &&
                nearly_equal(toy3d::cos(toy3d::Radians()), 1.0f),
            "Angle arithmetic and trigonometry must preserve explicit units");
        check(toy3d::is_nearly_equal(
                    quarter_turn, toy3d::Radians(toy3d::k_half_pi)) &&
                !toy3d::is_finite(toy3d::Degrees(
                    std::numeric_limits<float>::infinity())),
            "Angle predicates must preserve finite and tolerance semantics");
    }

    void check_vector_contract()
    {
        toy3d::Vector3 value(3.0f, 4.0f, 0.0f);
        check(toy3d::Vector3() == toy3d::Vector3(0.0f) &&
                value.data()[0] == 3.0f &&
                value.data()[1] == 4.0f &&
                value.data()[2] == 0.0f,
            "Vector3 must default to zero and expose contiguous scalar data");
        check(toy3d::dot(value, toy3d::Vector3(1.0f, 2.0f, 3.0f)) == 11.0f &&
                toy3d::length_squared(value) == 25.0f &&
                toy3d::distance(value, toy3d::Vector3()) == 5.0f,
            "Vector3 dot, length, and distance must use component values");

        const toy3d::Vector3 right = toy3d::cross(
            toy3d::Vector3(0.0f, 1.0f, 0.0f),
            toy3d::Vector3(0.0f, 0.0f, 1.0f));
        check(right == toy3d::Vector3(1.0f, 0.0f, 0.0f) &&
                toy3d::cross(
                    right,
                    toy3d::Vector3(0.0f, 1.0f, 0.0f)) ==
                    toy3d::Vector3(0.0f, 0.0f, 1.0f),
            "Vector3 cross must retain the left-handed canonical basis");

        toy3d::Vector3 normalized(9.0f);
        check(toy3d::try_normalize(value, normalized) &&
                toy3d::is_nearly_equal(
                    normalized, toy3d::Vector3(0.6f, 0.8f, 0.0f)),
            "try_normalize must produce a finite unit Vector3");

        toy3d::Vector3 unchanged(7.0f, 8.0f, 9.0f);
        check(!toy3d::try_normalize(toy3d::Vector3(), unchanged) &&
                unchanged == toy3d::Vector3(7.0f, 8.0f, 9.0f) &&
                toy3d::normalized_or_zero(toy3d::Vector3()) ==
                    toy3d::Vector3(),
            "Degenerate normalization must preserve output or explicitly return zero");

        const float infinity = std::numeric_limits<float>::infinity();
        check(!toy3d::try_normalize(
                    toy3d::Vector3(infinity, 0.0f, 0.0f), unchanged) &&
                unchanged == toy3d::Vector3(7.0f, 8.0f, 9.0f),
            "Non-finite normalization must fail without modifying output");

        check(toy3d::Vector2(1.0f, 2.0f) * 2.0f ==
                    toy3d::Vector2(2.0f, 4.0f) &&
                toy3d::Vector4(toy3d::Vector3(1.0f, 2.0f, 3.0f), 4.0f) ==
                    toy3d::Vector4(1.0f, 2.0f, 3.0f, 4.0f) &&
                toy3d::UIntVector3(1, 2, 3).data()[2] == 3,
            "Vector2, Vector4, and UIntVector values must retain explicit components");
    }

    void check_quaternion_contract()
    {
        toy3d::Quaternion x_rotation;
        check(toy3d::try_make_quaternion_from_axis_angle(
                    toy3d::Vector3(1.0f, 0.0f, 0.0f),
                    toy3d::Radians(toy3d::k_half_pi),
                    x_rotation) &&
                x_rotation.data()[0] > 0.0f &&
                x_rotation.data()[1] == 0.0f &&
                x_rotation.data()[2] == 0.0f &&
                x_rotation.data()[3] > 0.0f,
            "Axis-angle construction must use stable x, y, z, w storage");

        const toy3d::Vector3 rotated_forward = toy3d::rotate_vector(
            x_rotation, toy3d::Vector3(0.0f, 0.0f, 1.0f));
        check(toy3d::is_nearly_equal(
                    rotated_forward,
                    toy3d::Vector3(0.0f, -1.0f, 0.0f),
                    1.0e-5f) &&
                toy3d::is_nearly_equal(
                    toy3d::unrotate_vector(x_rotation, rotated_forward),
                    toy3d::Vector3(0.0f, 0.0f, 1.0f),
                    1.0e-5f),
            "Positive X rotation must match the existing +Z to -Y convention");

        const toy3d::Matrix3 rotation_matrix = toy3d::to_matrix3(x_rotation);
        toy3d::Quaternion matrix_rotation;
        check(toy3d::try_make_quaternion_from_rotation_matrix(
                    rotation_matrix, matrix_rotation) &&
                toy3d::is_nearly_same_rotation(
                    matrix_rotation, x_rotation, 1.0e-5f) &&
                toy3d::is_nearly_same_rotation(
                    x_rotation, -x_rotation, 1.0e-5f) &&
                toy3d::is_nearly_equal(
                    rotation_matrix * toy3d::Vector3(0.0f, 0.0f, 1.0f),
                    rotated_forward,
                    1.0e-5f),
            "Quaternion and Matrix3 conversion must preserve rotation and q/-q equivalence");
        check(toy3d::is_nearly_equal(
                    toy3d::transform_vector(
                        toy3d::to_matrix4(x_rotation),
                        toy3d::Vector3(0.0f, 0.0f, 1.0f)),
                    rotated_forward,
                    1.0e-5f),
            "Quaternion and Matrix4 vector rotation must agree");

        toy3d::Quaternion inverse;
        check(toy3d::try_inverse(x_rotation, inverse) &&
                toy3d::is_nearly_same_rotation(
                    x_rotation * inverse,
                    toy3d::Quaternion::identity(),
                    1.0e-5f),
            "Quaternion checked inverse must round-trip to identity rotation");

        toy3d::Quaternion between;
        check(toy3d::try_make_quaternion_between_directions(
                    toy3d::Vector3(0.0f, 0.0f, 1.0f),
                    toy3d::Vector3(1.0f, 0.0f, 0.0f),
                    between) &&
                toy3d::is_nearly_equal(
                    toy3d::rotate_vector(
                        between, toy3d::Vector3(0.0f, 0.0f, 1.0f)),
                    toy3d::Vector3(1.0f, 0.0f, 0.0f),
                    1.0e-5f),
            "Direction-to-direction construction must rotate source onto target");

        const toy3d::Quaternion unchanged = x_rotation;
        between = unchanged;
        check(!toy3d::try_make_quaternion_between_directions(
                    toy3d::Vector3(0.0f, 0.0f, 1.0f),
                    toy3d::Vector3(0.0f, 0.0f, -1.0f),
                    between) &&
                between == unchanged,
            "Opposite directions must fail atomically because their rotation axis is ambiguous");

        toy3d::Quaternion invalid_result = unchanged;
        check(!toy3d::try_make_quaternion_from_axis_angle(
                    toy3d::Vector3(),
                    toy3d::Radians(toy3d::k_half_pi),
                    invalid_result) &&
                invalid_result == unchanged &&
                !toy3d::try_inverse(toy3d::Quaternion(0.0f, 0.0f, 0.0f, 0.0f),
                    invalid_result) &&
                invalid_result == unchanged,
            "Degenerate Quaternion operations must fail without modifying output");

        toy3d::Quaternion halfway;
        check(toy3d::try_slerp(
                    toy3d::Quaternion::identity(), x_rotation, 0.5f, halfway) &&
                toy3d::is_nearly_equal(
                    toy3d::rotate_vector(
                        halfway, toy3d::Vector3(0.0f, 0.0f, 1.0f)),
                    toy3d::Vector3(
                        0.0f,
                        -std::sqrt(0.5f),
                        std::sqrt(0.5f)),
                    1.0e-5f),
            "Quaternion slerp must follow the shortest rotation arc");
        toy3d::Quaternion same_rotation_slerp;
        check(toy3d::try_slerp(
                    x_rotation, -x_rotation, 0.5f, same_rotation_slerp) &&
                toy3d::is_nearly_same_rotation(
                    same_rotation_slerp, x_rotation, 1.0e-5f),
            "Quaternion slerp must treat q and -q as the same endpoint rotation");

        toy3d::Matrix3 invalid_rotation(
            toy3d::Vector3(2.0f, 0.0f, 0.0f),
            toy3d::Vector3(0.0f, 1.0f, 0.0f),
            toy3d::Vector3(0.0f, 0.0f, 1.0f));
        matrix_rotation = unchanged;
        check(!toy3d::try_make_quaternion_from_rotation_matrix(
                    invalid_rotation, matrix_rotation) &&
                matrix_rotation == unchanged,
            "Scaled matrices must not be accepted as rotation matrices");
    }

    void check_transform_contract()
    {
        toy3d::Quaternion rotation;
        check(toy3d::try_make_quaternion_from_axis_angle(
                    toy3d::Vector3(1.0f, 0.0f, 0.0f),
                    toy3d::Radians(toy3d::k_half_pi),
                    rotation),
            "Transform test rotation must build");

        toy3d::Transform transform;
        transform.translation = toy3d::Vector3(3.0f, 4.0f, 5.0f);
        transform.rotation = rotation;
        transform.scale = toy3d::Vector3(2.0f, 3.0f, 4.0f);
        const toy3d::Vector3 position(1.0f, 0.0f, 1.0f);
        const toy3d::Vector3 transformed_position =
            toy3d::transform_position(transform, position);
        const toy3d::Vector3 transformed_vector =
            toy3d::transform_vector(transform, position);
        const toy3d::Vector3 transformed_direction =
            toy3d::transform_direction(
                transform, toy3d::Vector3(0.0f, 0.0f, 1.0f));
        check(toy3d::is_nearly_equal(
                    transformed_position,
                    toy3d::Vector3(5.0f, 0.0f, 5.0f),
                    1.0e-5f) &&
                toy3d::is_nearly_equal(
                    transformed_vector,
                    toy3d::Vector3(2.0f, -4.0f, 0.0f),
                    1.0e-5f) &&
                toy3d::is_nearly_equal(
                    transformed_direction,
                    toy3d::Vector3(0.0f, -1.0f, 0.0f),
                    1.0e-5f),
            "Transform position, vector, and direction must distinguish translation and scale");
        check(toy3d::is_nearly_equal(
                    toy3d::inverse_transform_position(
                        transform, transformed_position),
                    position,
                    1.0e-5f) &&
                toy3d::is_nearly_equal(
                    toy3d::inverse_transform_vector(
                        transform, transformed_vector),
                    position,
                    1.0e-5f) &&
                toy3d::is_nearly_equal(
                    toy3d::forward(transform),
                    transformed_direction,
                    1.0e-5f),
            "Transform inverse operations and axes must round-trip without scale leakage");

        const toy3d::Matrix4 matrix = toy3d::to_matrix(transform);
        check(toy3d::is_nearly_equal(
                    toy3d::transform_position(matrix, position),
                    transformed_position,
                    1.0e-5f),
            "Transform and Matrix4 position transforms must agree");
        toy3d::Transform decomposed;
        check(toy3d::try_decompose_transform(matrix, decomposed) &&
                toy3d::is_nearly_equal(
                    toy3d::to_matrix(decomposed), matrix, 1.0e-5f),
            "Positive-scale TRS must round-trip through checked decomposition");

        toy3d::Transform unchanged = transform;
        toy3d::Matrix4 shear = matrix;
        shear.at(1, 0) += 0.5f;
        check(!toy3d::try_decompose_transform(shear, unchanged) &&
                toy3d::is_nearly_equal(
                    toy3d::to_matrix(unchanged), matrix, 1.0e-5f),
            "Shear must fail decomposition without modifying output");
        toy3d::Matrix4 mirrored;
        mirrored.at(0, 0) = -1.0f;
        check(!toy3d::try_decompose_transform(mirrored, unchanged) &&
                toy3d::is_nearly_equal(
                    toy3d::to_matrix(unchanged), matrix, 1.0e-5f),
            "A mirrored matrix without a positive-scale TRS representation must fail atomically");
    }
}

int main()
{
    check_matrix_contract();
    check_matrix_inverse_and_transform_contract();
    check_view_contract();
    check_projection_contract();
    check_plane_and_convex_volume_contract();
    check_scalar_contract();
    check_angle_contract();
    check_vector_contract();
    check_quaternion_contract();
    check_transform_contract();
    if (failure_count != 0)
    {
        std::cerr << failure_count << " Core Math check(s) failed.\n";
        return 1;
    }
    std::cout << "Core Math checks passed.\n";
    return 0;
}
