#pragma once

#include "random.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "glm/glm.hpp"

namespace toy3d
{
    static const float POSITIVE_INF     = std::numeric_limits<float>::infinity();
    static const float NEGTIVE_INF      = -std::numeric_limits<float>::infinity();
    static const float PI               = 3.14159265358979323846264338327950288f;
    static const float INVIRSE_PI       = 1.0f / PI;
    static const float TWO_PI           = 2.0f * PI;
    static const float HALF_PI          = 0.5f * PI;
    static const float DEG2RAD          = PI / 180.0f;
    static const float RAD2DEG          = 180.0f / PI;
    static const float LOG2             = log(2.0f);
    static const float EPSILON          = 1e-6f;

    static const float F_EPSILON  = FLT_EPSILON;
    static const float D_EPSILON = DBL_EPSILON;

    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat3x3 = glm::mat3x3;
    using mat4x4 = glm::mat4x4;
    using mat3x4 = glm::mat3x4;
    using mat4x3 = glm::mat4x3;
    using quat = glm::quat;
    using color = glm::vec4;

    class Degree;
    class Radian;
    class Angle;
    class Math;

    class Radian
    {
        float m_rad;

    public:
        Radian(float r = 0) : m_rad(r) {}
        Radian(const Degree& d);
        Radian& operator=(float f)
        {
            m_rad = f;
            return *this;
        }
        Radian& operator=(const Degree& d);

        float get_rad() const { return m_rad; }
        float get_deg() const; // see bottom of this file
        float get_angle() const;

        const Radian& operator+() const { return *this; }
        Radian        operator+(const Radian& r) const { return Radian(m_rad + r.m_rad); }
        Radian        operator+(const Degree& d) const;
        Radian&       operator+=(const Radian& r)
        {
            m_rad += r.m_rad;
            return *this;
        }
        Radian& operator+=(const Degree& d);
        Radian  operator-() const { return Radian(-m_rad); }
        Radian  operator-(const Radian& r) const { return Radian(m_rad - r.m_rad); }
        Radian  operator-(const Degree& d) const;
        Radian& operator-=(const Radian& r)
        {
            m_rad -= r.m_rad;
            return *this;
        }
        Radian& operator-=(const Degree& d);
        Radian  operator*(float f) const { return Radian(m_rad * f); }
        Radian  operator*(const Radian& f) const { return Radian(m_rad * f.m_rad); }
        Radian& operator*=(float f)
        {
            m_rad *= f;
            return *this;
        }
        Radian  operator/(float f) const { return Radian(m_rad / f); }
        Radian& operator/=(float f)
        {
            m_rad /= f;
            return *this;
        }

        bool operator<(const Radian& r) const { return m_rad < r.m_rad; }
        bool operator<=(const Radian& r) const { return m_rad <= r.m_rad; }
        bool operator==(const Radian& r) const { return m_rad == r.m_rad; }
        bool operator!=(const Radian& r) const { return m_rad != r.m_rad; }
        bool operator>=(const Radian& r) const { return m_rad >= r.m_rad; }
        bool operator>(const Radian& r) const { return m_rad > r.m_rad; }
    };

    /** Wrapper class which indicates a given angle value is in Degrees.
    @remarks
        Degree values are interchangeable with Radian values, and conversions
        will be done automatically between them.
    */
    class Degree
    {
        float m_deg; // if you get an error here - make sure to define/typedef 'float' first

    public:
        explicit Degree(float d = 0) : m_deg(d) {}
        explicit Degree(const Radian& r) : m_deg(r.get_deg()) {}
        Degree& operator=(float f)
        {
            m_deg = f;
            return *this;
        }
        Degree& operator=(const Degree& d) = default;
        Degree& operator=(const Radian& r)
        {
            m_deg = r.get_deg();
            return *this;
        }

        float get_deg() const { return m_deg; }
        float get_rad() const; // see bottom of this file
        float get_angle() const;

        const Degree& operator+() const { return *this; }
        Degree        operator+(const Degree& d) const { return Degree(m_deg + d.m_deg); }
        Degree        operator+(const Radian& r) const { return Degree(m_deg + r.get_deg()); }
        Degree&       operator+=(const Degree& d)
        {
            m_deg += d.m_deg;
            return *this;
        }
        Degree& operator+=(const Radian& r)
        {
            m_deg += r.get_deg();
            return *this;
        }
        Degree  operator-() const { return Degree(-m_deg); }
        Degree  operator-(const Degree& d) const { return Degree(m_deg - d.m_deg); }
        Degree  operator-(const Radian& r) const { return Degree(m_deg - r.get_deg()); }
        Degree& operator-=(const Degree& d)
        {
            m_deg -= d.m_deg;
            return *this;
        }
        Degree& operator-=(const Radian& r)
        {
            m_deg -= r.get_deg();
            return *this;
        }
        Degree  operator*(float f) const { return Degree(m_deg * f); }
        Degree  operator*(const Degree& f) const { return Degree(m_deg * f.m_deg); }
        Degree& operator*=(float f)
        {
            m_deg *= f;
            return *this;
        }
        Degree  operator/(float f) const { return Degree(m_deg / f); }
        Degree& operator/=(float f)
        {
            m_deg /= f;
            return *this;
        }

        bool operator<(const Degree& d) const { return m_deg < d.m_deg; }
        bool operator<=(const Degree& d) const { return m_deg <= d.m_deg; }
        bool operator==(const Degree& d) const { return m_deg == d.m_deg; }
        bool operator!=(const Degree& d) const { return m_deg != d.m_deg; }
        bool operator>=(const Degree& d) const { return m_deg >= d.m_deg; }
        bool operator>(const Degree& d) const { return m_deg > d.m_deg; }
    };

    /** Wrapper class which identifies a value as the currently default angle
        type, as defined by Math::setAngleUnit.
    @remarks
        Angle values will be automatically converted between radians and degrees,
        as appropriate.
    */
    class Angle
    {
        float m_angle;

    public:
        explicit Angle(float angle) : m_angle(angle) {}
        Angle() { m_angle = 0; }

        explicit operator Radian() const;
        explicit operator Degree() const;
    };

    class Math
    {
    private:
        enum class AngleUnit
        {
            AU_DEGREE,
            AU_RADIAN
        };

        // angle units used by the api
        static AngleUnit k_AngleUnit;

    public:
        Math();

        static float abs(float value) { return std::fabs(value); }
        static bool  is_nan(float f) { return std::isnan(f); }
        static float sqr(float value) { return value * value; }
        static float sqrt(float fValue) { return std::sqrt(fValue); }
        static float inv_sqrt(float value) { return 1.f / sqrt(value); }
        static bool  equal(float a, float b, float tolerance = std::numeric_limits<float>::epsilon());
        static float clamp(float v, float min, float max) { return std::clamp(v, min, max); }
        static float max(float x, float y, float z) { return std::max({x, y, z}); }

        static float deg2rad(float degrees);
        static float rad2deg(float radians);
        static float ang2rad(float units);
        static float rad2ang(float radians);
        static float ang2deg(float units);
        static float deg2ang(float degrees);

        static float  sin(const Radian& rad) { return std::sin(rad.get_rad()); }
        static float  sin(float value) { return std::sin(value); }
        static float  cos(const Radian& rad) { return std::cos(rad.get_rad()); }
        static float  cos(float value) { return std::cos(value); }
        static float  tan(const Radian& rad) { return std::tan(rad.get_rad()); }
        static float  tan(float value) { return std::tan(value); }
        static Radian acos(float value);
        static Radian asin(float value);
        static Radian atan(float value) { return Radian(std::atan(value)); }
        static Radian atan2(float y_v, float x_v) { return Radian(std::atan2(y_v, x_v)); }

        template<class T>
        static constexpr T max(const T A, const T B)
        {
            return std::max(A, B);
        }

        template<class T>
        static constexpr T min(const T A, const T B)
        {
            return std::min(A, B);
        }

        template<class T>
        static constexpr T max3(const T A, const T B, const T C)
        {
            return std::max({A, B, C});
        }

        template<class T>
        static constexpr T min3(const T A, const T B, const T C)
        {
            return std::min({A, B, C});
        }

        // static mat4x4
        // view_matrix(const vec3& position, const quat& orientation, const mat4x4* reflect_matrix = nullptr);

        // static mat4x4
        // lookat(const vec3& eye_position, const vec3& target_position, const vec3& up_dir);

        // static mat4x4 perspective_matrix(Radian fovy, float aspect, float znear, float zfar);

        // static mat4x4
        // orthographic_matrix(float left, float right, float bottom, float top, float znear, float zfar);
    };

    // these functions could not be defined within the class definition of class
    // Radian because they required class Degree to be defined
    inline Radian::Radian(const Degree& d) : m_rad(d.get_rad()) {}
    inline Radian& Radian::operator=(const Degree& d)
    {
        m_rad = d.get_rad();
        return *this;
    }
    inline Radian Radian::operator+(const Degree& d) const { return Radian(m_rad + d.get_rad()); }
    inline Radian& Radian::operator+=(const Degree& d)
    {
        m_rad += d.get_rad();
        return *this;
    }
    inline Radian Radian::operator-(const Degree& d) const { return Radian(m_rad - d.get_rad()); }
    inline Radian& Radian::operator-=(const Degree& d)
    {
        m_rad -= d.get_rad();
        return *this;
    }

    inline float Radian::get_deg() const { return Math::rad2deg(m_rad); }

    inline float Radian::get_angle() const { return Math::rad2ang(m_rad); }

    inline float Degree::get_rad() const { return Math::deg2rad(m_deg); }

    inline float Degree::get_angle() const { return Math::deg2rad(m_deg); }

    inline Angle::operator Radian() const { return Radian(Math::ang2rad(m_angle)); }

    inline Angle::operator Degree() const { return Degree(Math::ang2deg(m_angle)); }

    inline Radian operator*(float a, const Radian& b) { return Radian(a * b.get_rad()); }

    inline Radian operator/(float a, const Radian& b) { return Radian(a / b.get_rad()); }

    inline Degree operator*(float a, const Degree& b) { return Degree(a * b.get_deg()); }

    inline Degree operator/(float a, const Degree& b) { return Degree(a / b.get_deg()); }
} // namespace toy3d
