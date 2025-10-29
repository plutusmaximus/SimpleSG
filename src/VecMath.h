#pragma once

#include <cmath>
#include <numbers>

#include "glm/glm.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/matrix_clip_space.hpp"

template<typename T> class Radians;

template <typename T>
class Degrees
{
public:

    Degrees() = default;

    explicit constexpr Degrees(const T value) : m_Value(value)
    {
    }

    Degrees<T>& operator=(const T other)
    {
        m_Value = other;
        return *this;
    }

    static constexpr Degrees<T> FromRadians(const Radians<T> radians);

    static constexpr Degrees<T> FromRadians(const T radians);

    constexpr Radians<T> ToRadians() const;

    constexpr Degrees<T> operator+(const Degrees<T> other) const
    {
        return Degrees<T>(m_Value + other.m_Value);
    }

    constexpr Degrees<T> operator+(const T other) const
    {
        return Degrees<T>(m_Value + other);
    }

    constexpr Degrees<T> operator-(const Degrees<T> other) const
    {
        return Degrees<T>(m_Value - other.m_Value);
    }

    constexpr Degrees<T> operator-(const T other) const
    {
        return Degrees<T>(m_Value - other);
    }

    constexpr Degrees<T> operator-() const
    {
        return Degrees<T>(-m_Value);
    }

    constexpr Degrees<T>& operator+=(const Degrees<T> other)
    {
        m_Value += other.m_Value;
        return *this;
    }

    constexpr Degrees<T>& operator+=(const T other)
    {
        m_Value += other;
        return *this;
    }

    constexpr Degrees<T>& operator-=(const Degrees<T> other)
    {
        m_Value -= other.m_Value;
        return *this;
    }

    constexpr Degrees<T>& operator-=(const T other)
    {
        m_Value -= other;
        return *this;
    }

    constexpr bool operator==(const T other) const
    {
        // Using a small epsilon for floating-point comparison
        constexpr T EPSILON = static_cast<T>(1e-10);
        return std::abs(m_Value - other.m_Value) < EPSILON;
    }

    constexpr bool operator!=(const Degrees<T> other) const
    {
        return !(*this == other);
    }

    constexpr T Value() const
    {
        return m_Value;
    }

    Degrees<T> Wrap() const
    {
        T t = m_Value;
        while (t < MIN) { t += MAX; }
        while (t > MAX) { t -= MAX; }
        return Degrees<T>(t);
    }

private:
    static constexpr T MAX = 360;
    static constexpr T MIN = -360;

    T m_Value;
};

template <typename T>
class Radians
{
public:

    Radians() = default;

    explicit constexpr Radians(const T value) : m_Value(value)
    {
    }

    Radians<T>& operator=(const T other)
    {
        m_Value = other;
        return *this;
    }

    static constexpr Radians<T> FromDegrees(const Degrees<T> degrees)
    {
        return FromDegrees(degrees.Value());
    }

    static constexpr Radians<T> FromDegrees(const T degrees)
    {
        return Radians<T>(degrees * std::numbers::pi_v<T> / 180);
    }

    constexpr Degrees<T> ToDegrees() const
    {
        return Degrees<T>::FromRadians(*this);
    }

    constexpr Radians<T> operator+(const Radians<T> other) const
    {
        return Radians<T>(m_Value + other.m_Value);
    }

    constexpr Radians<T> operator+(const T other) const
    {
        return Radians<T>(m_Value + other);
    }

    constexpr Radians<T> operator-(const Radians<T> other) const
    {
        return Radians<T>(m_Value - other.m_Value);
    }

    constexpr Radians<T> operator-(const T other) const
    {
        return Radians<T>(m_Value - other);
    }

    constexpr Radians<T> operator-() const
    {
        return Radians<T>(-m_Value);
    }

    constexpr Radians<T>& operator+=(const Radians<T> other)
    {
        m_Value += other.m_Value;
        return *this;
    }

    constexpr Radians<T>& operator+=(const T other)
    {
        m_Value += other;
        return *this;
    }

    constexpr Radians<T>& operator-=(const Radians<T> other)
    {
        m_Value -= other.value;
        return *this;
    }

    constexpr Radians<T>& operator-=(const T other)
    {
        m_Value -= other;
        return *this;
    }

    constexpr bool operator==(const T other) const
    {
        // Using a small epsilon for floating-point comparison
        constexpr T EPSILON = static_cast<T>(1e-10);
        return std::abs(m_Value - other.m_Value) < EPSILON;
    }

    constexpr bool operator!=(const Radians<T> other) const
    {
        return !(*this == other);
    }

    constexpr T Value() const
    {
        return m_Value;
    }

    Radians<T> Wrap() const
    {
        T t = m_Value;
        while (t < MIN) { t += MAX; }
        while (t > MAX) { t -= MAX; }
        return Radians<T>(t);
    }

private:
    static constexpr T MAX = 2 * std::numbers::pi_v<T>;
    static constexpr T MIN = -2 * std::numbers::pi_v<T>;
    T m_Value;
};

template<typename T>
inline constexpr Degrees<T> Degrees<T>::FromRadians(Radians<T> radians)
{
    return FromRadians(radians.Value());
}

template<typename T>
inline constexpr Degrees<T> Degrees<T>::FromRadians(const T radians)
{
    return Degrees<T>(radians * 180 / std::numbers::pi_v<T>);
}

template<typename T>
inline constexpr Radians<T> Degrees<T>::ToRadians() const
{
    return Radians<T>::FromDegrees(*this);
}

template<typename T>
class Vec3 : public glm::vec<3, T>
{
public:

    constexpr Vec3(const glm::vec<3, T>& v)
        : glm::vec<3, T>(v)
    {
    }

    using glm::vec<3, T>::vec;
    using glm::vec<3, T>::x;
    using glm::vec<3, T>::y;
    using glm::vec<3, T>::z;

    static inline consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static inline consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static inline consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    Vec3 Normalize() const
    {
        return glm::normalize(*this);
    }

    constexpr Vec3 Scale(const T scale)
    {
        return glm::scale(scale, *this);
    }
    
    constexpr Vec3 Cross(const Vec3& that) const noexcept
    {
        return glm::cross(*this, that);
    }

    constexpr T Dot(const Vec3& that) const
    {
        return glm::dot(*this, that);
    }

    constexpr Vec3 operator+(const Vec3& that) const
    {
        return glm::operator+(*this, that);
    }

    constexpr Vec3 operator-(const Vec3& that) const
    {
        return glm::operator-(*this, that);
    }
};

template<typename T>
class Vec4 : public glm::vec<4, T>
{
public:

    constexpr Vec4(const glm::vec<4, T>& v)
        : glm::vec<4, T>(v)
    {
    }

    using glm::vec<4, T>::vec;
    using glm::vec<4, T>::x;
    using glm::vec<4, T>::y;
    using glm::vec<4, T>::z;
    using glm::vec<4, T>::w;

    Vec4 Normalize() const
    {
        return glm::normalize(*this);
    }

    constexpr Vec4 Scale(const T scale)
    {
        return glm::scale(scale, *this);
    }

    constexpr Vec4 Cross(const Vec4& that) const noexcept
    {
        return glm::cross(*this, that);
    }

    constexpr T Dot(const Vec4& that) const
    {
        return glm::dot(*this, that);
    }

    constexpr Vec4 operator+(const Vec4& that) const
    {
        return glm::operator+(*this, that);
    }

    constexpr Vec4 operator-(const Vec4& that) const
    {
        return glm::operator-(*this, that);
    }
};

// 4x4 column-major matrix
template<typename T>
class Mat44 : public glm::mat<4, 4, T>
{
public:

    constexpr Mat44(const glm::mat<4, 4, T>& m)
        : glm::mat<4, 4, T>(m)
    {
    }

    using glm::mat<4, 4, T>::mat;

    Mat44 Mul(const Mat44& other) const
    {
        return *this * other;
    }

    Vec4<T> Mul(const Vec4<T>& vector) const
    {
        return *this * vector;
    }

    Vec4<T> Mul(const Vec3<T>& vector) const
    {
        return *this * vector;
    }

    Mat44 Translate(const Vec3<T>& t) const
    {
        return glm::translate(*this, t);
    }

    Mat44 Translate(const T tx, const T ty, const T tz) const
    {
        return glm::translate(*this, glm::vec<3, T>(tx, ty, tz));
    }

    Mat44 Rotate(const Degrees<T> deg, const Vec3<T>& axis) const
    {
        return Rotate(deg.ToRadians(), axis);
    }

    Mat44 Rotate(const Radians<T> rad, const Vec3<T>& axis) const
    {
        return glm::rotate(*this, rad.Value(), axis);
    }

    Mat44 Scale(const T scalar) const
    {
        return glm::scale(*this, glm::vec<3, T>(scalar));
    }

    Mat44 Inverse() const
    {
        return glm::inverse(*this);
    }

    Mat44 Transpose() const
    {
        return glm::transpose(*this);
    }

    static const Mat44& Identity()
    {
        static constexpr Mat44 IDENT = glm::mat<4, 4, T>(1);

        return IDENT;
    }

    static Mat44 PerspectiveRH(const Degrees<T> fov, const T width, const T height, const T nearClip, const T farClip)
    {
        return PerspectiveRH(fov.ToRadians(), width, height, nearClip, farClip);
    }

    static Mat44 PerspectiveRH(const Radians<T> fov, const T width, const T height, const T nearClip, const T farClip)
    {
        return glm::perspectiveFovRH(fov.Value(), width, height, nearClip, farClip);
    }

    static Mat44 PerspectiveLH(const Degrees<T> fov, const T width, const T height, const T nearClip, const T farClip)
    {
        return PerspectiveLH(fov.ToRadians(), width, height, nearClip, farClip);
    }

    static Mat44 PerspectiveLH(const Radians<T> fov, const T width, const T height, const T nearClip, const T farClip)
    {
        return glm::perspectiveFovLH(fov.Value(), width, height, nearClip, farClip);
    }
};

using Degreesf = Degrees<float>;
using Radiansf = Radians<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Mat44f = Mat44<float>;