#pragma once

#include "AssertHelper.h"

#include <numbers>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <type_traits>

template<typename T> class Vec2;
template<typename T> class Vec3;
template<typename T> class Vec4;
template<typename T> class Mat44;

template <typename T>
class Radians
{
    static_assert(std::is_floating_point_v<T>, "Radians requires a floating-point type");

    static constexpr T TWO_PI = 2 * std::numbers::pi_v<T>;
    static constexpr T PI = std::numbers::pi_v<T>;
    static constexpr T eps = T(8) * std::numeric_limits<T>::epsilon() * TWO_PI;

    // Wraps the input value to the range [-π, π)
    constexpr static T Wrap(const T value)
    {
        // Wrap to [-π, π)
        const T r = value - std::floor((value + PI) / TWO_PI) * TWO_PI;

        if (r <= -PI + eps)
        {
            return PI;
        }
        else if (std::abs(r) < eps)
        {
            return T{0};
        }

        return r;
    }

    constexpr static T Limit(const T value)
    {
        if(value > 1000 * TWO_PI)
        {
            return value - (1000 * TWO_PI);
        }
        else if(value < -1000 * TWO_PI)
        {
            return value + (1000 * TWO_PI);
        }
        else
        {
            return value;
        }
    }

public:

    constexpr Radians() = default;

    explicit constexpr Radians(const T value)
     : m_Value(Limit(value))
    {
    }

    constexpr Radians<T>& operator=(const T other)
    {
        m_Value = Limit(other);
        return *this;
    }

    static constexpr Radians<T> FromDegrees(const T degrees)
    {
        return Radians<T>(degrees * std::numbers::pi_v<T> / 180);
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

    constexpr Radians<T> operator*(const T other) const
    {
        return Radians<T>(m_Value * other);
    }

    constexpr Radians<T> operator-() const
    {
        return Radians<T>(-m_Value);
    }

    constexpr Radians<T>& operator+=(const Radians<T> other)
    {
        return *this = *this + other;
    }

    constexpr Radians<T>& operator+=(const T other)
    {
        return *this = *this + other;
    }

    constexpr Radians<T>& operator-=(const Radians<T> other)
    {
        return *this = *this - other;
    }

    constexpr Radians<T>& operator-=(const T other)
    {
        return *this = *this - other;
    }

    constexpr Radians<T>& operator*=(const T other)
    {
        return *this = *this * other;
    }

    constexpr bool operator==(const Radians<T> other) const
    {
        // Using a small epsilon for floating-point comparison
        constexpr T EPSILON = static_cast<T>(1e-10);
        return std::abs(m_Value - other.m_Value) < EPSILON;
    }

    constexpr bool operator==(const T other) const
    {
        // Using a small epsilon for floating-point comparison
        constexpr T EPSILON = static_cast<T>(1e-10);
        return std::abs(m_Value - other) < EPSILON;
    }

    constexpr bool operator!=(const Radians<T> other) const
    {
        return !(*this == other);
    }

    constexpr bool operator!=(const T other) const
    {
        return !(*this == other);
    }

    constexpr T GetValue() const
    {
        return Wrap(m_Value);
    }

private:

    T m_Value{0};
};

template<typename T>
inline constexpr Radians<T> operator*(const T a, const Radians<T> b)
{
    return b * a;
}

template<typename T>
class Vec2
{
    static_assert(std::is_floating_point_v<T>, "Vec2 requires a floating-point type");
public:

    T x, y;

    constexpr Vec2() = default;

    constexpr explicit Vec2(T value)
        : x(value), y(value)
    {
    }
    constexpr Vec2(T x, T y)
        : x(x), y(y)
    {
    }

    constexpr explicit Vec2(const Vec3<T>& v);

    constexpr explicit Vec2(const Vec4<T>& v);

    constexpr Vec2<T> Normalize() const
    {
        return *this / Length();
    }

    constexpr T Length() const
    {
        return std::sqrt(x * x + y * y);
    }

    constexpr T Length2() const
    {
        return x * x + y * y;
    }

    constexpr Vec2 Cross(const Vec2& that) const
    {
        return Vec2(x * that.y - y * that.x, y * that.x - x * that.y);
    }

    constexpr T Dot(const Vec2& that) const
    {
        return x * that.x + y * that.y;
    }

    constexpr bool operator==(const Vec2& that) const
    {
        return x == that.x && y == that.y;
    }

    constexpr Vec2 operator+(const Vec2& that) const
    {
        return Vec2(x + that.x, y + that.y);
    }

    constexpr Vec2 operator-(const Vec2& that) const
    {
        return Vec2(x - that.x, y - that.y);
    }

    constexpr Vec2 operator*(const Vec2& that) const
    {
        return Vec2(x * that.x, y * that.y);
    }

    constexpr Vec2 operator*(const T scalar) const
    {
        return Vec2(x * scalar, y * scalar);
    }

    constexpr Vec2 operator/(const T scalar) const
    {
        MLG_ASSERT(scalar != T{ 0 }, "Division by zero in Vec2 operator/");
        const T invScalar = T{1} / scalar;
        return Vec2(x * invScalar, y * invScalar);
    }

    friend constexpr Vec2 operator*(const T scalar, const Vec2& v)
    {
        return v * scalar;
    }

    friend constexpr Vec2 operator/(const T scalar, const Vec2& v)
    {
        MLG_ASSERT(v.x != T{ 0 } && v.y != T{ 0 }, "Division by zero in Vec2 operator/");
        return Vec2(scalar / v.x, scalar / v.y);
    }

    constexpr Vec2 operator-() const
    {
        return Vec2(-x, -y);
    }

    constexpr T& operator[](size_t index)
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr const T& operator[](size_t index) const
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec2& operator+=(const Vec2& that)
    {
        return *this = *this + that;
    }

    constexpr Vec2& operator-=(const Vec2& that)
    {
        return *this = *this - that;
    }

    constexpr Vec2& operator*=(const Vec2& that)
    {
        return *this = *this * that;
    }

    constexpr Vec2<T>& operator*=(const T scalar)
    {
        return *this = *this * scalar;
    }

    constexpr Vec2<T>& operator/=(const T scalar)
    {
        return *this = *this / scalar;
    }
};

template<typename T>
class Vec3
{
    static_assert(std::is_floating_point_v<T>, "Vec3 requires a floating-point type");

public:

    T x, y, z;

    constexpr Vec3() = default;

    constexpr explicit Vec3(T value)
        : x(value), y(value), z(value)
    {
    }
    constexpr Vec3(T x, T y, T z)
        : x(x), y(y), z(z)
    {
    }

    constexpr Vec3(const Vec2<T>& v, T z)
        : x(v.x), y(v.y), z(z)
    {
    }

    constexpr explicit Vec3(const Vec4<T>& v);

    static inline consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static inline consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static inline consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    constexpr Vec3 Normalize() const
    {
        return *this / Length();
    }

    constexpr T Length() const
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    constexpr T Length2() const
    {
        return x * x + y * y + z * z;
    }

    constexpr Vec3 Cross(const Vec3& that) const
    {
        return Vec3(
            y * that.z - z * that.y,
            z * that.x - x * that.z,
            x * that.y - y * that.x
        );
    }

    constexpr T Dot(const Vec3& that) const
    {
        return x * that.x + y * that.y + z * that.z;
    }

    /// Rotates this vector around the given axis by the specified angle.
    constexpr Vec3 RotateBy(const Vec3& axis, const Radians<T> angle) const
    {
        // https://en.wikipedia.org/wiki/Rodrigues'_rotation_formula
        const T radVal = angle.GetValue();
        const Vec3 vr = *this * std::cos(radVal) + axis.Cross(*this) * std::sin(radVal) +
                         axis * (axis.Dot(*this)) * (1 - std::cos(radVal));
        return vr;
    }

    constexpr bool operator==(const Vec3& that) const
    {
        return x == that.x && y == that.y && z == that.z;
    }

    constexpr Vec3 operator+(const Vec3& that) const
    {
        return Vec3(x + that.x, y + that.y, z + that.z);
    }

    constexpr Vec3 operator-(const Vec3& that) const
    {
        return Vec3(x - that.x, y - that.y, z - that.z);
    }

    constexpr Vec3 operator*(const Vec3& that) const
    {
        return Vec3(x * that.x, y * that.y, z * that.z);
    }

    constexpr Vec3 operator*(const T scalar) const
    {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }

    constexpr Vec3 operator/(const T scalar) const
    {
        MLG_ASSERT(scalar != T{ 0 }, "Division by zero in Vec3 operator/");
        const T invScalar = T{1} / scalar;
        return Vec3(x * invScalar, y * invScalar, z * invScalar);
    }

    friend constexpr Vec3 operator*(const T scalar, const Vec3& v)
    {
        return v * scalar;
    }

    friend constexpr Vec3 operator/(const T scalar, const Vec3& v)
    {
        MLG_ASSERT(v.x != T{ 0 } && v.y != T{ 0 } && v.z != T{ 0 },
            "Division by zero in Vec3 operator/");
        return Vec3(scalar / v.x, scalar / v.y, scalar / v.z);
    }

    constexpr Vec3 operator-() const
    {
        return Vec3(-x, -y, -z);
    }

    constexpr T& operator[](size_t index)
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr const T& operator[](size_t index) const
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec3& operator+=(const Vec3& that)
    {
        return *this = *this + that;
    }

    constexpr Vec3& operator-=(const Vec3& that)
    {
        return *this = *this - that;
    }

    constexpr Vec3& operator*=(const Vec3& that)
    {
        return *this = *this * that;
    }

    constexpr Vec3& operator*=(const T scalar)
    {
        return *this = *this * scalar;
    }

    constexpr Vec3& operator/=(const T scalar)
    {
        return *this = *this / scalar;
    }
};

template<typename T>
class Vec4
{
    static_assert(std::is_floating_point_v<T>, "Vec4 requires a floating-point type");

public:

    T x, y, z, w;

    constexpr Vec4() = default;

    constexpr explicit Vec4(T value)
        : x(value), y(value), z(value), w(value)
    {
    }

    constexpr Vec4(T x, T y, T z, T w)
        : x(x), y(y), z(z), w(w)
    {
    }

    constexpr Vec4(const Vec2<T>& v, T z, T w)
        : x(v.x), y(v.y), z(z), w(w)
    {
    }

    constexpr Vec4(const Vec3<T>& v, T w)
        : x(v.x), y(v.y), z(v.z), w(w)
    {
    }

    constexpr Vec4 Normalize() const
    {
        return *this / Length();
    }

    constexpr T Length() const
    {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    constexpr T Length2() const
    {
        return x * x + y * y + z * z + w * w;
    }

    constexpr T Dot(const Vec4& that) const
    {
        return x * that.x + y * that.y + z * that.z + w * that.w;
    }

    constexpr bool operator==(const Vec4& that) const
    {
        return x == that.x && y == that.y && z == that.z && w == that.w;
    }

    constexpr Vec4 operator+(const Vec4& that) const
    {
        return Vec4(x + that.x, y + that.y, z + that.z, w + that.w);
    }

    constexpr Vec4 operator-(const Vec4& that) const
    {
        return Vec4(x - that.x, y - that.y, z - that.z, w - that.w);
    }

    constexpr Vec4 operator*(const Vec4& that) const
    {
        return Vec4(x * that.x, y * that.y, z * that.z, w * that.w);
    }

    constexpr Vec4 operator*(const T scalar) const
    {
        return Vec4(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    constexpr Vec4 operator/(const T scalar) const
    {
        MLG_ASSERT(scalar != T{ 0 }, "Division by zero in Vec4 operator/");
        const T invScalar = T{1} / scalar;
        return Vec4(x * invScalar, y * invScalar, z * invScalar, w * invScalar);
    }

    friend constexpr Vec4 operator*(const T scalar, const Vec4& v)
    {
        return v * scalar;
    }

    friend constexpr Vec4 operator/(const T scalar, const Vec4& v)
    {
        MLG_ASSERT(v.x != T{ 0 } && v.y != T{ 0 } && v.z != T{ 0 } && v.w != T{ 0 },
            "Division by zero in Vec4 operator/");
        return Vec4(scalar / v.x, scalar / v.y, scalar / v.z, scalar / v.w);
    }

    constexpr Vec4 operator-() const
    {
        return Vec4(-x, -y, -z, -w);
    }

    constexpr T& operator[](size_t index)
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        case 3: return w;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr const T& operator[](size_t index) const
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        case 3: return w;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec4& operator+=(const Vec4& that)
    {
        return *this = *this + that;
    }

    constexpr Vec4& operator-=(const Vec4& that)
    {
        return *this = *this - that;
    }

    constexpr Vec4& operator*=(const Vec4& that)
    {
        return *this = *this * that;
    }

    constexpr Vec4& operator*=(const T scalar)
    {
        return *this = *this * scalar;
    }

    constexpr Vec4& operator/=(const T scalar)
    {
        return *this = *this / scalar;
    }
};

template<typename T>
inline constexpr Vec2<T>::Vec2(const Vec3<T>& v)
    : x(v.x), y(v.y)
{
}

template<typename T>
inline constexpr Vec2<T>::Vec2(const Vec4<T>& v)
    : x(v.x), y(v.y)
{
}

template<typename T>
inline constexpr Vec3<T>::Vec3(const Vec4<T>& v)
    : x(v.x), y(v.y), z(v.z)
{
}

template<typename T>
class Quat
{
    static_assert(std::is_floating_point_v<T>, "Quat requires a floating-point type");

public:

    constexpr Quat() = default;

    constexpr Quat(T x, T y, T z, T w)
        : x(x), y(y), z(z), w(w)
    {
    }

    constexpr Quat(const Radians<T> angle, const Vec3<T>& axis)
    {
        const T ao2 = angle.GetValue() * T{0.5};
        const auto s = std::sin(ao2);
        x = axis.x * s;
        y = axis.y * s;
        z = axis.z * s;
        w = std::cos(ao2);
    }

    constexpr Mat44<T> ToMatrix() const;

    constexpr Quat Normalize() const
    {
        const T len = x * x + y * y + z * z + w * w;
        MLG_ASSERT(len != T{ 0 }, "Cannot normalize a zero-length quaternion");
        const T invLen = T{1} / std::sqrt(len);
        return Quat(x * invLen, y * invLen, z * invLen, w * invLen);
    }

    constexpr Quat Conjugate() const
    {
        return Quat(-x, -y, -z, w);
    }

    constexpr bool operator==(const Quat& that) const
    {
        return x == that.x && y == that.y && z == that.z && w == that.w;
    }

    constexpr Vec3<T> operator*(const Vec3<T>& v) const
    {
        // Assumes a unit quaternion.
        const Vec3<T> u{ x, y, z };
        const Vec3<T> t = u.Cross(v) * T{2};
        return v + w * t + u.Cross(t);
    }

    constexpr Quat operator*(const T scalar) const
    {
        return Quat(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    constexpr Quat operator*(const Quat& that) const
    {
        return Quat(
            w * that.x + x * that.w + y * that.z - z * that.y,
            w * that.y - x * that.z + y * that.w + z * that.x,
            w * that.z + x * that.y - y * that.x + z * that.w,
            w * that.w - x * that.x - y * that.y - z * that.z
        ).Normalize();
    }

    constexpr Quat operator+(const Quat& that) const
    {
        return Quat(x + that.x, y + that.y, z + that.z, w + that.w);
    }

    constexpr Quat operator-(const Quat& that) const
    {
        return Quat(x - that.x, y - that.y, z - that.z, w - that.w);
    }

    constexpr Quat operator-() const
    {
        return Quat(-x, -y, -z, -w);
    }

    constexpr Quat& operator*=(const T scalar)
    {
        return (*this = *this * scalar);
    }

    constexpr Quat& operator*=(const Quat& that)
    {
        return (*this = *this * that);
    }

    constexpr Quat& operator+=(const Quat& that)
    {
        return (*this = *this + that);
    }

    constexpr Quat& operator-=(const Quat& that)
    {
        return (*this = *this - that);
    }

//private:

    T x{0}, y{0}, z{0}, w{1};
};

// 4x4 column-major matrix
template<typename T>
class Mat44
{
    static_assert(std::is_floating_point_v<T>, "Mat44 requires a floating-point type");

public:

    Vec4<T> m[4];

    constexpr Mat44() = default;

    constexpr Mat44(
        const Vec4<T>& col0, const Vec4<T>& col1, const Vec4<T>& col2, const Vec4<T>& col3)
    {
        m[0] = col0;
        m[1] = col1;
        m[2] = col2;
        m[3] = col3;
    }

    constexpr explicit Mat44(T value)
        : Mat44(Vec4<T>(value, 0, 0, 0),
                Vec4<T>(0, value, 0, 0),
                Vec4<T>(0, 0, value, 0),
                Vec4<T>(0, 0, 0, value))
    {
    }

    constexpr Mat44(T m00, T m01, T m02, T m03,
          T m10, T m11, T m12, T m13,
          T m20, T m21, T m22, T m23,
          T m30, T m31, T m32, T m33)
          : Mat44(Vec4<T>(m00, m01, m02, m03),
                 Vec4<T>(m10, m11, m12, m13),
                 Vec4<T>(m20, m21, m22, m23),
                 Vec4<T>(m30, m31, m32, m33))
    {
    }

    constexpr Mat44 Mul(const Mat44& other) const
    {
        return *this * other;
    }

    constexpr Vec4<T> Mul(const Vec4<T>& vector) const
    {
        return *this * vector;
    }

    constexpr Vec4<T> Mul(const Vec3<T>& vector) const
    {
        return *this * vector;
    }

    constexpr bool operator==(const Mat44& that) const
    {
        return 0 == std::memcmp(this, &that, sizeof(*this));
    }

    constexpr Mat44& operator*=(const Mat44& that)
    {
        return *this = *this * that;
    }

    constexpr Mat44 Inverse() const
    {
        const auto& mm = *this;

        const T m00 = mm[0][0], m01 = mm[0][1], m02 = mm[0][2], m03 = mm[0][3];
        const T m10 = mm[1][0], m11 = mm[1][1], m12 = mm[1][2], m13 = mm[1][3];
        const T m20 = mm[2][0], m21 = mm[2][1], m22 = mm[2][2], m23 = mm[2][3];
        const T m30 = mm[3][0], m31 = mm[3][1], m32 = mm[3][2], m33 = mm[3][3];

        T inv[16];
        inv[0]  =  m11 * (m22 * m33 - m23 * m32) - m21 * (m12 * m33 - m13 * m32) + m31 * (m12 * m23 - m13 * m22);
        inv[4]  = -m10 * (m22 * m33 - m23 * m32) + m20 * (m12 * m33 - m13 * m32) - m30 * (m12 * m23 - m13 * m22);
        inv[8]  =  m10 * (m21 * m33 - m23 * m31) - m20 * (m11 * m33 - m13 * m31) + m30 * (m11 * m23 - m13 * m21);
        inv[12] = -m10 * (m21 * m32 - m22 * m31) + m20 * (m11 * m32 - m12 * m31) - m30 * (m11 * m22 - m12 * m21);

        inv[1]  = -m01 * (m22 * m33 - m23 * m32) + m21 * (m02 * m33 - m03 * m32) - m31 * (m02 * m23 - m03 * m22);
        inv[5]  =  m00 * (m22 * m33 - m23 * m32) - m20 * (m02 * m33 - m03 * m32) + m30 * (m02 * m23 - m03 * m22);
        inv[9]  = -m00 * (m21 * m33 - m23 * m31) + m20 * (m01 * m33 - m03 * m31) - m30 * (m01 * m23 - m03 * m21);
        inv[13] =  m00 * (m21 * m32 - m22 * m31) - m20 * (m01 * m32 - m02 * m31) + m30 * (m01 * m22 - m02 * m21);

        inv[2]  =  m01 * (m12 * m33 - m13 * m32) - m11 * (m02 * m33 - m03 * m32) + m31 * (m02 * m13 - m03 * m12);
        inv[6]  = -m00 * (m12 * m33 - m13 * m32) + m10 * (m02 * m33 - m03 * m32) - m30 * (m02 * m13 - m03 * m12);
        inv[10] =  m00 * (m11 * m33 - m13 * m31) - m10 * (m01 * m33 - m03 * m31) + m30 * (m01 * m13 - m03 * m11);
        inv[14] = -m00 * (m11 * m32 - m12 * m31) + m10 * (m01 * m32 - m02 * m31) - m30 * (m01 * m12 - m02 * m11);

        inv[3]  = -m01 * (m12 * m23 - m13 * m22) + m11 * (m02 * m23 - m03 * m22) - m21 * (m02 * m13 - m03 * m12);
        inv[7]  =  m00 * (m12 * m23 - m13 * m22) - m10 * (m02 * m23 - m03 * m22) + m20 * (m02 * m13 - m03 * m12);
        inv[11] = -m00 * (m11 * m23 - m13 * m21) + m10 * (m01 * m23 - m03 * m21) - m20 * (m01 * m13 - m03 * m11);
        inv[15] =  m00 * (m11 * m22 - m12 * m21) - m10 * (m01 * m22 - m02 * m21) + m20 * (m01 * m12 - m02 * m11);

        const T det = m00 * inv[0] + m01 * inv[4] + m02 * inv[8] + m03 * inv[12];
        if (det == 0)
        {
            return Mat44(0);
        }

        const T invDet = 1 / det;
        Mat44 result(0);
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                result[c][r] = inv[c * 4 + r] * invDet;
            }
        }
        return result;
    }

    constexpr Mat44 Transpose() const
    {
        return Mat44 //
            {
                Vec4<T>(m[0].x, m[1].x, m[2].x, m[3].x),
                Vec4<T>(m[0].y, m[1].y, m[2].y, m[3].y),
                Vec4<T>(m[0].z, m[1].z, m[2].z, m[3].z),
                Vec4<T>(m[0].w, m[1].w, m[2].w, m[3].w),
            };
    }

    constexpr void Decompose(Vec3<T>& translation, Quat<T>& rotation, Vec3<T>& scale) const
    {
        const auto& mm = *this;

        // Extract translation (column 3)
        translation = Vec3<T>(mm[3][0], mm[3][1], mm[3][2]);

        // Extract scale (length of basis columns)
        scale.x = std::sqrt(mm[0][0] * mm[0][0] + mm[0][1] * mm[0][1] + mm[0][2] * mm[0][2]);
        scale.y = std::sqrt(mm[1][0] * mm[1][0] + mm[1][1] * mm[1][1] + mm[1][2] * mm[1][2]);
        scale.z = std::sqrt(mm[2][0] * mm[2][0] + mm[2][1] * mm[2][1] + mm[2][2] * mm[2][2]);

        // Build rotation matrix (row-major) from normalized columns
        const T invX = scale.x != 0 ? 1 / scale.x : 0;
        const T invY = scale.y != 0 ? 1 / scale.y : 0;
        const T invZ = scale.z != 0 ? 1 / scale.z : 0;

        const T r00 = mm[0][0] * invX;
        const T r01 = mm[1][0] * invY;
        const T r02 = mm[2][0] * invZ;
        const T r10 = mm[0][1] * invX;
        const T r11 = mm[1][1] * invY;
        const T r12 = mm[2][1] * invZ;
        const T r20 = mm[0][2] * invX;
        const T r21 = mm[1][2] * invY;
        const T r22 = mm[2][2] * invZ;

        // Convert rotation matrix to quaternion
        const T trace = r00 + r11 + r22;
        T qw, qx, qy, qz;
        if (trace > 0)
        {
            const T s = std::sqrt(trace + 1) * 2;
            qw = T{0.25} * s;
            qx = (r21 - r12) / s;
            qy = (r02 - r20) / s;
            qz = (r10 - r01) / s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            const T s = std::sqrt(1 + r00 - r11 - r22) * 2;
            qw = (r21 - r12) / s;
            qx = T{0.25} * s;
            qy = (r01 + r10) / s;
            qz = (r02 + r20) / s;
        }
        else if (r11 > r22)
        {
            const T s = std::sqrt(1 + r11 - r00 - r22) * 2;
            qw = (r02 - r20) / s;
            qx = (r01 + r10) / s;
            qy = T{0.25} * s;
            qz = (r12 + r21) / s;
        }
        else
        {
            const T s = std::sqrt(1 + r22 - r00 - r11) * 2;
            qw = (r10 - r01) / s;
            qx = (r02 + r20) / s;
            qy = (r12 + r21) / s;
            qz = T{0.25} * s;
        }

        rotation = Quat<T>(qx, qy, qz, qw);
    }

    constexpr Vec4<T>& operator[](std::size_t index)
    {
        return m[index];
    }

    constexpr const Vec4<T>& operator[](std::size_t index) const
    {
        return m[index];
    }

    constexpr Mat44 operator*(const Mat44& that) const
    {
        return Mat44 //
            { *this * that[0], *this * that[1], *this * that[2], *this * that[3] };
    }

    constexpr Vec4<T> operator*(const Vec4<T>& vector) const
    {
        return m[0] * vector.x + m[1] * vector.y + m[2] * vector.z + m[3] * vector.w;
    }

    constexpr Vec4<T> operator*(const Vec3<T>& vector) const
    {
        return Vec4<T>(m[0] * vector.x + m[1] * vector.y + m[2] * vector.z + m[3]);
    }

    static constexpr const Mat44& Identity()
    {
        static constexpr Mat44 IDENT(1);

        return IDENT;
    }

    static Mat44 PerspectiveLH(const Radians<T> fov, const T aspectRatio, const T nearClip, const T farClip)
    {
        const T rad = fov.GetValue();
        const T t = std::tan(rad/2);

        Mat44 result(0);
        result[0][0] = static_cast<T>(1.0 / (t * aspectRatio));
        result[1][1] = static_cast<T>(1.0 / t);
        result[2][2] = farClip / (farClip - nearClip);
        result[2][3] = 1;
        result[3][2] = -(farClip * nearClip) / (farClip - nearClip);
        return result;
    }
};

template<typename T>
inline constexpr Mat44<T> Quat<T>::ToMatrix() const
{
    const T xx = x * x;
    const T yy = y * y;
    const T zz = z * z;
    const T xy = x * y;
    const T xz = x * z;
    const T yz = y * z;
    const T wx = w * x;
    const T wy = w * y;
    const T wz = w * z;

    // Column-major 4x4 rotation matrix
    return Mat44<T> //
        {
            Vec4<T>(1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0),
            Vec4<T>(2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0),
            Vec4<T>(2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0),
            Vec4<T>(0, 0, 0, 1),
        };
}

template<typename NumType>
class TrsTransform
{
    static_assert(std::is_floating_point_v<NumType>, "TrsTransform requires a floating-point type");

public:

    Vec3<NumType> T{ 0 };
    Quat<NumType> R{ Radians<NumType>{0}, Vec3<NumType>{0,1,0} };
    Vec3<NumType> S{ 1 };

    constexpr Mat44<NumType> ToMatrix() const
    {
        Mat44<NumType> M = R.ToMatrix();
        M[0] *= S.x;
        M[1] *= S.y;
        M[2] *= S.z;
        M[3] = Vec4<NumType>(T, 1);
        return M;
    }

    // The inverse of a TRS transform cannot in general be represented as a TRS transform,
    // unless the scale is uniform.
    // So the inverse returns a Mat44, which can represent any affine transform,
    // instead of a new TrsTransform.
    constexpr Mat44<NumType> Inverse() const
    {
        TrsTransform result;
        result.R = R.Normalize().Conjugate();
        result.S = NumType{1} / S;
        result.T = -(result.R * (T * result.S));
        return result.ToMatrix();
    }

    static TrsTransform FromMatrix(const Mat44<NumType>& mat)
    {
        TrsTransform result;
        mat.Decompose(result.T, result.R, result.S);
        return result;
    }

    constexpr Vec3<NumType> LocalXAxis() const
    {
        return (R * Vec3<NumType>::XAXIS()).Normalize();
    }

    constexpr Vec3<NumType> LocalYAxis() const
    {
        return (R * Vec3<NumType>::YAXIS()).Normalize();
    }

    constexpr Vec3<NumType> LocalZAxis() const
    {
        return (R * Vec3<NumType>::ZAXIS()).Normalize();
    }

    constexpr Vec3<NumType> operator*(const Vec3<NumType>& point) const
    {
        return R * (point * S) + T;
    }

    constexpr bool operator==(const TrsTransform<NumType>& that) const
    {
        return T == that.T && R == that.R && S == that.S;
    }
};

class Extent
{
public:

    float Width;
    float Height;

    constexpr bool operator==(const Extent& that) const
    {
        return Width == that.Width && Height == that.Height;
    }
    constexpr bool operator!=(const Extent& that) const
    {
        return !(*this == that);
    }
};

class Point
{
public:

    float X;
    float Y;

    constexpr bool operator==(const Point& that) const
    {
        return X == that.X && Y == that.Y;
    }
    constexpr bool operator!=(const Point& that) const
    {
        return !(*this == that);
    }
};

using Radiansf = Radians<float>;
using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Quatf = Quat<float>;
using Mat44f = Mat44<float>;
using TrsTransformf = TrsTransform<float>;

/// @brief Enable formatting of Vec2 via std::format.
template<typename T>
struct std::formatter<Vec2<T>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const Vec2<T>& vec, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {})", vec.x, vec.y);
    }
};

/// @brief Enable formatting of Vec3 via std::format.
template<typename T>
struct std::formatter<Vec3<T>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const Vec3<T>& vec, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {}, {})", vec.x, vec.y, vec.z);
    }
};

/// @brief Enable formatting of Vec4 via std::format.
template<typename T>
struct std::formatter<Vec4<T>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const Vec4<T>& vec, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w);
    }
};

/// @brief Enable formatting of Quat via std::format.
template<typename T>
struct std::formatter<Quat<T>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const Quat<T>& quat, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {}, {}, {})", quat.x, quat.y, quat.z, quat.w);
    }
};

/// @brief Enable formatting of Extent via std::format.
template<>
struct std::formatter<Extent>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const Extent& extent, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {})", extent.Width, extent.Height);
    }
};

/// @brief Enable formatting of Point via std::format.
template<>
struct std::formatter<Point>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const Point& point, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {})", point.X, point.Y);
    }
};