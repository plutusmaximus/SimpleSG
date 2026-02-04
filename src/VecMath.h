#pragma once

#include "Error.h"

#include <numbers>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <type_traits>

template <typename T>
class Radians
{
    static constexpr T MAX = 2 * std::numbers::pi_v<T>;

    constexpr static T Wrap(const T value)
    {
        return value - (static_cast<int>(value / MAX) * MAX);
    }

public:

    constexpr Radians() = default;

    explicit constexpr Radians(const T value)
     : m_Value(Wrap(value))
    {
    }

    constexpr Radians<T>& operator=(const T other)
    {
        m_Value = other;
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

    constexpr T Value() const
    {
        return m_Value;
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
public:

    T x;
    T y;

    constexpr Vec2() = default;

    constexpr Vec2(T value)
        : x(value), y(value)
    {
    }
    constexpr Vec2(T x, T y)
        : x(x), y(y)
    {
    }

    constexpr Vec2<T> Normalize() const
    {
        const T length = std::sqrt(x * x + y * y);
        return Vec2<T>(x / length, y / length);
    }

    constexpr T Length() const
    {
        return std::sqrt(x * x + y * y);
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
        default: eassert(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr const T& operator[](size_t index) const
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        default: eassert(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec2& operator+=(const Vec2& that)
    {
        x += that.x;
        y += that.y;
        return *this;
    }

    constexpr Vec2& operator-=(const Vec2& that)
    {
        x -= that.x;
        y -= that.y;
        return *this;
    }

    constexpr Vec2& operator*=(const Vec2& that)
    {
        x *= that.x;
        y *= that.y;
        return *this;
    }

    constexpr Vec2<T>& operator*=(const T scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }
};

template<typename T>
inline constexpr Vec2<T> operator*(const T a, const Vec2<T> b)
{
    return b * a;
}

template<typename T>
class Vec3
{
public:

    T x;
    T y;
    T z;

    constexpr Vec3() = default;

    constexpr Vec3(T value)
        : x(value), y(value), z(value)
    {
    }
    constexpr Vec3(T x, T y, T z)
        : x(x), y(y), z(z)
    {
    }

    static inline consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static inline consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static inline consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    constexpr Vec3 Normalize() const
    {
        const T length = std::sqrt(x * x + y * y + z * z);
        return Vec3(x / length, y / length, z / length);
    }

    constexpr T Length() const
    {
        return std::sqrt(x * x + y * y + z * z);
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
        default: eassert(false, "Index out of range");
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
        default: eassert(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec3& operator+=(const Vec3& that)
    {
        x += that.x;
        y += that.y;
        z += that.z;
        return *this;
    }

    constexpr Vec3& operator-=(const Vec3& that)
    {
        x -= that.x;
        y -= that.y;
        z -= that.z;
        return *this;
    }

    constexpr Vec3& operator*=(const Vec3& that)
    {
        x *= that.x;
        y *= that.y;
        z *= that.z;
        return *this;
    }

    constexpr Vec3& operator*=(const T scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
};

template<typename T>
inline constexpr Vec3<T> operator*(const T a, const Vec3<T> b)
{
    return b * a;
}

template<typename T>
class Vec4
{
public:

    T x;
    T y;
    T z;
    T w;

    constexpr Vec4() = default;

    constexpr Vec4(T value)
        : x(value), y(value), z(value), w(value)
    {
    }

    constexpr Vec4(T x, T y, T z, T w)
        : x(x), y(y), z(z), w(w)
    {
    }

    constexpr Vec4 Normalize() const
    {
        const T length = std::sqrt(x * x + y * y + z * z + w * w);
        return Vec4(x / length, y / length, z / length, w / length);
    }

    constexpr T Length() const
    {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    /*constexpr Vec4 Cross(const Vec4& that) const
    {
        return glm::cross(*this, that);
    }*/

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
        default: eassert(false, "Index out of range");
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
        default: eassert(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec4& operator+=(const Vec4& that)
    {
        x += that.x;
        y += that.y;
        z += that.z;
        w += that.w;
        return *this;
    }

    constexpr Vec4& operator-=(const Vec4& that)
    {
        x -= that.x;
        y -= that.y;
        z -= that.z;
        w -= that.w;
        return *this;
    }

    constexpr Vec4& operator*=(const Vec4& that)
    {
        x *= that.x;
        y *= that.y;
        z *= that.z;
        w *= that.w;
        return *this;
    }

    constexpr Vec4& operator*=(const T scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }
};

template<typename T>
inline constexpr Vec4<T> operator*(const T a, const Vec4<T> b)
{
    return b * a;
}

template<typename T>
class Quat
{
public:

    T x;
    T y;
    T z;
    T w;

    constexpr Quat() = default;

    constexpr Quat(T x, T y, T z, T w)
        : x(x), y(y), z(z), w(w)
    {
    }

    constexpr Quat(const Radians<T> angle, const Vec3<T>& axis)
    {
        const T ao2 = angle.Value() / 2;
        const auto s = std::sin(ao2);
        x = axis.x * s;
        y = axis.y * s;
        z = axis.z * s;
        w = std::cos(ao2);
    }

    constexpr Quat Normalize() const
    {
        const T length = std::sqrt(x * x + y * y + z * z + w * w);
        return Quat(x / length, y / length, z / length, w / length);
    }

    float GetRotation(const Vec3<T>& axis) const
    {
        const auto normalizedThis = this->Normalize();
        const auto rotatedVec = normalizedThis * axis;
        const T dotProduct = axis.Dot(rotatedVec);
        return std::acos(dotProduct) * 2;
    }

    constexpr Quat Conjugate() const
    {
        return Quat(-x, -y, -z, w);
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

    constexpr bool operator==(const Quat& that) const
    {
        return x == that.x && y == that.y && z == that.z && w == that.w;
    }

    constexpr Vec3<T> operator*(const Vec3<T>& v) const
    {
        const auto qv = Quat<T>(v.x, v.y, v.z, static_cast<T>(0));
        const auto result = (*this * qv * this->Conjugate()).Normalize();
        return Vec3<T>(result.x, result.y, result.z);
    }

    constexpr Quat operator-(const Quat& that) const
    {
        return Quat(x - that.x, y - that.y, z - that.z, w - that.w);
    }

    constexpr Quat operator-(const Vec3<T>& v) const
    {
        return Quat(x - v.x, y - v.y, z - v.z, w);
    }

    constexpr Quat operator-(const Vec4<T>& v) const
    {
        return Quat(x - v.x, y - v.y, z - v.z, w - v.w);
    }

    constexpr Quat operator-() const
    {
        return Quat(-x, -y, -z, -w);
    }

    constexpr Quat& operator*=(const Quat& that)
    {
        return (*this = *this * that);
    }
};

// 4x4 column-major matrix
template<typename T>
class Mat44
{
public:

    Vec4<T> m[4];

    constexpr Mat44() = default;

    constexpr explicit Mat44(T value)
    {
        m[0][1] = m[0][2] = m[0][3] = static_cast<T>(0);
        m[1][0] = m[1][2] = m[1][3] = static_cast<T>(0);
        m[2][0] = m[2][1] = m[2][3] = static_cast<T>(0);
        m[3][0] = m[3][1] = m[3][2] = static_cast<T>(0);

        m[0][0] = m[1][1] = m[2][2] = m[3][3] = value;
    }

    constexpr Mat44(T m00, T m01, T m02, T m03,
          T m10, T m11, T m12, T m13,
          T m20, T m21, T m22, T m23,
          T m30, T m31, T m32, T m33)
    {
        m[0][0] = m00; m[0][1] = m01; m[0][2] = m02; m[0][3] = m03;
        m[1][0] = m10; m[1][1] = m11; m[1][2] = m12; m[1][3] = m13;
        m[2][0] = m20; m[2][1] = m21; m[2][2] = m22; m[2][3] = m23;
        m[3][0] = m30; m[3][1] = m31; m[3][2] = m32; m[3][3] = m33;
    }

    constexpr explicit Mat44(const Quat<T>& q)
    {
        const T x = q.x;
        const T y = q.y;
        const T z = q.z;
        const T w = q.w;

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
        m[0][0] = static_cast<T>(1) - static_cast<T>(2) * (yy + zz);
        m[0][1] = static_cast<T>(2) * (xy + wz);
        m[0][2] = static_cast<T>(2) * (xz - wy);
        m[0][3] = static_cast<T>(0);

        m[1][0] = static_cast<T>(2) * (xy - wz);
        m[1][1] = static_cast<T>(1) - static_cast<T>(2) * (xx + zz);
        m[1][2] = static_cast<T>(2) * (yz + wx);
        m[1][3] = static_cast<T>(0);

        m[2][0] = static_cast<T>(2) * (xz + wy);
        m[2][1] = static_cast<T>(2) * (yz - wx);
        m[2][2] = static_cast<T>(1) - static_cast<T>(2) * (xx + yy);
        m[2][3] = static_cast<T>(0);

        m[3][0] = static_cast<T>(0);
        m[3][1] = static_cast<T>(0);
        m[3][2] = static_cast<T>(0);
        m[3][3] = static_cast<T>(1);
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
        if (det == static_cast<T>(0))
        {
            return Mat44(static_cast<T>(0));
        }

        const T invDet = static_cast<T>(1) / det;
        Mat44 result(static_cast<T>(0));
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
        const auto& mm = *this;
        Mat44 result(static_cast<T>(0));
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                result[r][c] = mm[c][r];
            }
        }
        return result;
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
        const T invX = scale.x != static_cast<T>(0) ? static_cast<T>(1) / scale.x : static_cast<T>(0);
        const T invY = scale.y != static_cast<T>(0) ? static_cast<T>(1) / scale.y : static_cast<T>(0);
        const T invZ = scale.z != static_cast<T>(0) ? static_cast<T>(1) / scale.z : static_cast<T>(0);

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
        if (trace > static_cast<T>(0))
        {
            const T s = std::sqrt(trace + static_cast<T>(1)) * static_cast<T>(2);
            qw = static_cast<T>(0.25) * s;
            qx = (r21 - r12) / s;
            qy = (r02 - r20) / s;
            qz = (r10 - r01) / s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            const T s = std::sqrt(static_cast<T>(1) + r00 - r11 - r22) * static_cast<T>(2);
            qw = (r21 - r12) / s;
            qx = static_cast<T>(0.25) * s;
            qy = (r01 + r10) / s;
            qz = (r02 + r20) / s;
        }
        else if (r11 > r22)
        {
            const T s = std::sqrt(static_cast<T>(1) + r11 - r00 - r22) * static_cast<T>(2);
            qw = (r02 - r20) / s;
            qx = (r01 + r10) / s;
            qy = static_cast<T>(0.25) * s;
            qz = (r12 + r21) / s;
        }
        else
        {
            const T s = std::sqrt(static_cast<T>(1) + r22 - r00 - r11) * static_cast<T>(2);
            qw = (r10 - r01) / s;
            qx = (r02 + r20) / s;
            qy = (r12 + r21) / s;
            qz = static_cast<T>(0.25) * s;
        }

        rotation.x = qx;
        rotation.y = qy;
        rotation.z = qz;
        rotation.w = qw;
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
        const auto& a = *this;
        const auto& b = that;

        Mat44 result(0);
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                result[c][r] = a[0][r] * b[c][0]
                            + a[1][r] * b[c][1]
                            + a[2][r] * b[c][2]
                            + a[3][r] * b[c][3];
            }
        }
        return result;
    }

    constexpr Vec4<T> operator*(const Vec4<T>& vector) const
    {
        const auto& mm = *this;
        return Vec4<T>(
            mm[0][0] * vector.x + mm[1][0] * vector.y + mm[2][0] * vector.z + mm[3][0] * vector.w,
            mm[0][1] * vector.x + mm[1][1] * vector.y + mm[2][1] * vector.z + mm[3][1] * vector.w,
            mm[0][2] * vector.x + mm[1][2] * vector.y + mm[2][2] * vector.z + mm[3][2] * vector.w,
            mm[0][3] * vector.x + mm[1][3] * vector.y + mm[2][3] * vector.z + mm[3][3] * vector.w
        );
    }

    constexpr Vec4<T> operator*(const Vec3<T>& vector) const
    {
        const auto& mm = *this;
        return Vec4<T>(
            mm[0][0] * vector.x + mm[1][0] * vector.y + mm[2][0] * vector.z + mm[3][0],
            mm[0][1] * vector.x + mm[1][1] * vector.y + mm[2][1] * vector.z + mm[3][1],
            mm[0][2] * vector.x + mm[1][2] * vector.y + mm[2][2] * vector.z + mm[3][2],
            mm[0][3] * vector.x + mm[1][3] * vector.y + mm[2][3] * vector.z + mm[3][3]
        );
    }

    static constexpr const Mat44& Identity()
    {
        static constexpr Mat44 IDENT(1);

        return IDENT;
    }

    static Mat44 PerspectiveRH(const Radians<T> fov, const T width, const T height, const T nearClip, const T farClip)
    {
        const T rad = fov.Value();
        const T h = std::cos(static_cast<T>(0.5) * rad) / std::sin(static_cast<T>(0.5) * rad);
        const T w = h * height / width;

        Mat44 result(static_cast<T>(0));
        result[0][0] = w;
        result[1][1] = h;
        result[2][2] = farClip / (nearClip - farClip);
        result[2][3] = -static_cast<T>(1);
        result[3][2] = -(farClip * nearClip) / (farClip - nearClip);
        return result;
    }

    static Mat44 PerspectiveLH(const Radians<T> fov, const T width, const T height, const T nearClip, const T farClip)
    {
        const T rad = fov.Value();
        const T h = std::cos(static_cast<T>(0.5) * rad) / std::sin(static_cast<T>(0.5) * rad);
        const T w = h * height / width;

        Mat44 result(static_cast<T>(0));
        result[0][0] = w;
        result[1][1] = h;
        result[2][2] = farClip / (farClip - nearClip);
        result[2][3] = static_cast<T>(1);
        result[3][2] = -(farClip * nearClip) / (farClip - nearClip);
        return result;
    }
};

template<typename NumType>
class TrsTransform
{
public:

    Vec3<NumType> T{ 0 };
    Quat<NumType> R{ Radians<NumType>(0), Vec3<NumType>{0,1,0} };
    Vec3<NumType> S{ 1 };

    constexpr Mat44<NumType> ToMatrix() const
    {
        Mat44<NumType> M(R);
        M[0] *= S.x;
        M[1] *= S.y;
        M[2] *= S.z;
        M[3][0] = T.x;
        M[3][1] = T.y;
        M[3][2] = T.z;
        return M;
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

    constexpr bool operator==(const TrsTransform<NumType>& that) const
    {
        return T == that.T && R == that.R && S == that.S;
    }
};

struct Extent
{
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

struct Point
{
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