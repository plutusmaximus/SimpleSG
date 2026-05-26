#pragma once

#include "AssertHelper.h"

#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <cstddef>
#include <cstring>
#include <type_traits>

template<typename T> class Vec2;
template<typename T> class Vec3;
template<typename T> class Vec4;
template<typename T> class Mat44;

// For binary operators we prefer friend functions to allow for implicit conversions on both sides
// of the operator. For example:
//     Vec3f v(1.0f, 2.0f, 3.0f);
//     Vec3f result = 2.0f * v; // Implicitly converts 2.0f to Vec3f(2.0f, 2.0f, 2.0f) and calls
//     operator*(Vec3f, Vec3f)

// That's just an example.  In general it's an anti pattern to allow implicit conversions.
// But in cases where we do have implicit conversions we want binary operators to work.

template <typename T>
class Radians
{
private:
    static_assert(std::is_floating_point_v<T>, "Radians requires a floating-point type");

    static constexpr T TWO_PI = 2 * std::numbers::pi_v<T>;
    static constexpr T PI = std::numbers::pi_v<T>;
    static constexpr T eps = T(8) * std::numeric_limits<T>::epsilon() * TWO_PI;

    // Wraps the input value to the range [0, 2π)
    constexpr static T Wrap(const T value) noexcept
    {
        const T r = value - std::floor(value / TWO_PI) * TWO_PI;

        if((r >= TWO_PI - eps) || (std::abs(r) < eps))
        {
            return T{0};
        }

        return r;
    }

    constexpr static T Limit(const T value) noexcept
    {
        constexpr T kLimit = 1000 * TWO_PI;

        T newValue = value;

        while(newValue > kLimit)
        {
            newValue -= kLimit;
        }

        while(newValue < -kLimit)
        {
            newValue += kLimit;
        }

        return newValue;
    }

    struct PrivateCtorTag { };

    static constexpr PrivateCtorTag kPrivateCtorTag{};

    constexpr Radians(const uint32_t value, PrivateCtorTag) noexcept
     : m_Value(value)
    {
    }

public:

    using ValueType = T;

    constexpr Radians() = default;

    explicit constexpr Radians(const T value) noexcept
     : Radians(ToRep(value), kPrivateCtorTag)
    {
    }

    constexpr Radians<T>& operator=(const T value) noexcept
    {
        return *this = Radians(value);
    }

    static constexpr Radians<T> FromDegrees(const T degrees) noexcept
    {
        return Radians<T>(degrees * std::numbers::pi_v<T> / 180);
    }

    T Sin() const noexcept
    {
        return std::sin(FromRep(m_Value));
    }

    T Cos() const noexcept
    {
        return std::cos(FromRep(m_Value));
    }

    T Tan() const noexcept
    {
        return std::tan(FromRep(m_Value));
    }

    constexpr friend Radians<T> operator+(const Radians<T> a, const Radians<T> b) noexcept
    {
        return Radians<T>(a.m_Value + b.m_Value, kPrivateCtorTag);
    }

    constexpr friend Radians<T> operator+(const Radians<T> a, const T b) noexcept
    {
        return Radians<T>(a.m_Value + ToRep(b), kPrivateCtorTag);
    }

    constexpr friend Radians<T> operator+(const T a, const Radians<T> b) noexcept
    {
        return b + a;
    }

    constexpr friend Radians<T> operator-(const Radians<T> a, const Radians<T> b) noexcept
    {
        return Radians<T>(a.m_Value - b.m_Value, kPrivateCtorTag);
    }

    constexpr friend Radians<T> operator-(const Radians<T> a, const T b) noexcept
    {
        return Radians<T>(a.m_Value - ToRep(b), kPrivateCtorTag);
    }

    constexpr friend Radians<T> operator-(const T a, const Radians<T> b) noexcept
    {
        return -(b - a);
    }

    constexpr friend Radians<T> operator*(const Radians<T> a, const T b) noexcept
    {
        return Radians<T>(static_cast<RepType>(std::llround(static_cast<T>(a.m_Value) * b)), kPrivateCtorTag);
    }

    constexpr friend Radians<T> operator*(const T a, const Radians<T> b) noexcept
    {
        return b * a;
    }

    constexpr friend Radians<T> operator-(const Radians<T> a) noexcept
    {
        return Radians<T>(0u - a.m_Value, kPrivateCtorTag);
    }

    constexpr Radians<T>& operator+=(const Radians<T> other) noexcept
    {
        return *this = *this + other;
    }

    constexpr Radians<T>& operator+=(const T other) noexcept
    {
        return *this = *this + other;
    }

    constexpr Radians<T>& operator-=(const Radians<T> other) noexcept
    {
        return *this = *this - other;
    }

    constexpr Radians<T>& operator-=(const T other) noexcept
    {
        return *this = *this - other;
    }

    constexpr Radians<T>& operator*=(const T other) noexcept
    {
        return *this = *this * other;
    }

    constexpr friend bool operator==(const Radians<T> a, const Radians<T> b) noexcept
    {
        return a.m_Value == b.m_Value;
    }

    constexpr friend bool operator==(const Radians<T> a, const T b) noexcept
    {
        return a.m_Value == ToRep(b);
    }

    constexpr friend bool operator==(const T a, const Radians<T> b) noexcept
    {
        return b == a;
    }

    constexpr T GetValue() const noexcept
    {
        return FromRep(m_Value);
    }

private:

    using RepType = uint32_t;

    static constexpr T kFullTurn = 2 * std::numbers::pi_v<T>;
    static constexpr T kScale = 4294967296 / kFullTurn; // 2^32 / 2π
    static constexpr T kInvScale = kFullTurn / 4294967296; // 2π / 2^32

    constexpr static RepType ToRep(const T value) noexcept
    {
        return static_cast<RepType>(Wrap(Limit(value)) * kScale);
    }

    constexpr static T FromRep(const RepType value) noexcept
    {
        return static_cast<T>(value) * kInvScale;
    }

    RepType m_Value{0};
};

template<typename T>
class Vec2
{
    static_assert(std::is_floating_point_v<T>, "Vec2 requires a floating-point type");
public:

    using ValueType = T;

    T x, y;

    constexpr Vec2() = default;

    constexpr explicit Vec2(T value) noexcept
        : x(value), y(value)
    {
    }
    constexpr Vec2(const T inX, const T inY) noexcept
        : x(inX), y(inY)
    {
    }

    constexpr explicit Vec2(const Vec3<T>& v) noexcept;

    constexpr explicit Vec2(const Vec4<T>& v) noexcept;

    constexpr Vec2<T> Normalize() const noexcept
    {
        return *this / Length();
    }

    constexpr T Length() const noexcept
    {
        return std::sqrt(x * x + y * y);
    }

    constexpr T Length2() const noexcept

    {
        return x * x + y * y;
    }

    constexpr Vec2 Cross(const Vec2& that) const noexcept
    {
        return Vec2(x * that.y - y * that.x, y * that.x - x * that.y);
    }

    constexpr T Dot(const Vec2& that) const noexcept
    {
        return x * that.x + y * that.y;
    }

    constexpr Vec2 Lerp(const Vec2& that, T t) const noexcept
    {
        return Vec2(*this * (1 - t) + that * t);
    }

    constexpr friend bool operator==(const Vec2& a, const Vec2& b) noexcept
    {
        return a.x == b.x && a.y == b.y;
    }

    constexpr friend Vec2 operator+(const Vec2& a, const Vec2& b) noexcept
    {
        return Vec2(a.x + b.x, a.y + b.y);
    }

    constexpr friend Vec2 operator-(const Vec2& a, const Vec2& b) noexcept
    {
        return Vec2(a.x - b.x, a.y - b.y);
    }

    constexpr friend Vec2 operator*(const Vec2& a, const Vec2& b) noexcept
    {
        return Vec2(a.x * b.x, a.y * b.y);
    }

    constexpr friend Vec2 operator*(const Vec2& v, const T scalar) noexcept
    {
        return Vec2(v.x * scalar, v.y * scalar);
    }

    constexpr friend Vec2 operator*(const T scalar, const Vec2& v) noexcept
    {
        return v * scalar;
    }

    constexpr friend Vec2 operator/(const Vec2& v, const T scalar) noexcept
    {
        MLG_ASSERT(scalar != T{ 0 }, "Division by zero in Vec2 operator/");
        const T invScalar = T{1} / scalar;
        return Vec2(v.x * invScalar, v.y * invScalar);
    }

    constexpr friend Vec2 operator/(const T scalar, const Vec2& v) noexcept
    {
        MLG_ASSERT(v.x != T{ 0 } && v.y != T{ 0 }, "Division by zero in Vec2 operator/");
        return Vec2(scalar / v.x, scalar / v.y);
    }

    constexpr friend Vec2 operator-(const Vec2& v) noexcept
    {
        return Vec2(-v.x, -v.y);
    }

    constexpr T& operator[](size_t index) noexcept
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr const T& operator[](size_t index) const noexcept
    {
        switch (index)
        {
        case 0: return x;
        case 1: return y;
        default: MLG_ASSERT(false, "Index out of range");
        return x; // This line will never be reached, but it silences compiler warnings.
        }
    }

    constexpr Vec2& operator+=(const Vec2& that) noexcept
    {
        return *this = *this + that;
    }

    constexpr Vec2& operator-=(const Vec2& that) noexcept
    {
        return *this = *this - that;
    }

    constexpr Vec2& operator*=(const Vec2& that) noexcept
    {
        return *this = *this * that;
    }

    constexpr Vec2<T>& operator*=(const T scalar) noexcept
    {
        return *this = *this * scalar;
    }

    constexpr Vec2<T>& operator/=(const T scalar) noexcept
    {
        return *this = *this / scalar;
    }
};

template<typename T>
class Vec3
{
    static_assert(std::is_floating_point_v<T>, "Vec3 requires a floating-point type");

public:

    using ValueType = T;

    T x, y, z;

    constexpr Vec3() = default;

    constexpr explicit Vec3(T value) noexcept
        : x(value), y(value), z(value)
    {
    }
    constexpr Vec3(const T inX, const T inY, const T inZ) noexcept
        : x(inX), y(inY), z(inZ)
    {
    }

    constexpr Vec3(const Vec2<T>& v, const T inZ) noexcept
        : x(v.x), y(v.y), z(inZ)
    {
    }

    constexpr explicit Vec3(const Vec4<T>& v) noexcept;

    static inline consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static inline consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static inline consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    constexpr Vec3 Normalize() const noexcept
    {
        return *this / Length();
    }

    constexpr T Length() const noexcept
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    constexpr T Length2() const noexcept
    {
        return x * x + y * y + z * z;
    }

    constexpr Vec3 Cross(const Vec3& that) const noexcept
    {
        return Vec3(
            y * that.z - z * that.y,
            z * that.x - x * that.z,
            x * that.y - y * that.x
        );
    }

    constexpr T Dot(const Vec3& that) const noexcept
    {
        return x * that.x + y * that.y + z * that.z;
    }

    constexpr Vec3 Lerp(const Vec3& that, T t) const noexcept
    {
        return Vec3(*this * (1 - t) + that * t);
    }

    /// Rotates this vector around the given axis by the specified angle.
    constexpr Vec3 RotateBy(const Vec3& axis, const Radians<T> angle) const noexcept
    {
        // https://en.wikipedia.org/wiki/Rodrigues'_rotation_formula
        const T cos = angle.Cos();
        const T sin = angle.Sin();
        const Vec3 vr = *this * cos + axis.Cross(*this) * sin +
                         axis * (axis.Dot(*this)) * (1 - cos);
        return vr;
    }

    constexpr friend bool operator==(const Vec3& a, const Vec3& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    constexpr friend Vec3 operator+(const Vec3& a, const Vec3& b) noexcept
    {
        return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    constexpr friend Vec3 operator-(const Vec3& a, const Vec3& b) noexcept
    {
        return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    constexpr friend Vec3 operator*(const Vec3& a, const Vec3& b) noexcept
    {
        return Vec3(a.x * b.x, a.y * b.y, a.z * b.z);
    }

    constexpr friend Vec3 operator*(const Vec3& v, const T scalar) noexcept
    {
        return Vec3(v.x * scalar, v.y * scalar, v.z * scalar);
    }

    constexpr friend Vec3 operator*(const T scalar, const Vec3& v) noexcept
    {
        return Vec3(v.x * scalar, v.y * scalar, v.z * scalar);
    }

    constexpr friend Vec3 operator/(const Vec3& v, const T scalar) noexcept
    {
        MLG_ASSERT(scalar != T{ 0 }, "Division by zero in Vec3 operator/");
        const T invScalar = T{1} / scalar;
        return Vec3(v.x * invScalar, v.y * invScalar, v.z * invScalar);
    }

    constexpr friend Vec3 operator/(const T scalar, const Vec3& v) noexcept
    {
        MLG_ASSERT(v.x != T{ 0 } && v.y != T{ 0 } && v.z != T{ 0 },
            "Division by zero in Vec3 operator/");
        return Vec3(scalar / v.x, scalar / v.y, scalar / v.z);
    }

    constexpr friend Vec3 operator-(const Vec3& v) noexcept
    {
        return Vec3(-v.x, -v.y, -v.z);
    }

    constexpr T& operator[](size_t index) noexcept
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

    constexpr const T& operator[](size_t index) const noexcept
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

    constexpr Vec3& operator+=(const Vec3& that) noexcept
    {
        return *this = *this + that;
    }

    constexpr Vec3& operator-=(const Vec3& that) noexcept
    {
        return *this = *this - that;
    }

    constexpr Vec3& operator*=(const Vec3& that) noexcept
    {
        return *this = *this * that;
    }

    constexpr Vec3& operator*=(const T scalar) noexcept
    {
        return *this = *this * scalar;
    }

    constexpr Vec3& operator/=(const T scalar) noexcept
    {
        return *this = *this / scalar;
    }
};

template<typename T>
class Vec4
{
    static_assert(std::is_floating_point_v<T>, "Vec4 requires a floating-point type");

public:

    using ValueType = T;

    T x, y, z, w;

    constexpr Vec4() = default;

    constexpr explicit Vec4(T value) noexcept
        : x(value), y(value), z(value), w(value)
    {
    }

    constexpr Vec4(const T inX, const T inY, const T inZ, const T inW) noexcept
        : x(inX), y(inY), z(inZ), w(inW)
    {
    }

    constexpr Vec4(const Vec2<T>& v, const T inZ, const T inW) noexcept
        : x(v.x), y(v.y), z(inZ), w(inW)
    {
    }

    constexpr Vec4(const Vec3<T>& v, const T inW) noexcept
        : x(v.x), y(v.y), z(v.z), w(inW)
    {
    }

    constexpr Vec4 Normalize() const noexcept
    {
        return *this / Length();
    }

    constexpr T Length() const noexcept
    {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    constexpr T Length2() const noexcept
    {
        return x * x + y * y + z * z + w * w;
    }

    constexpr T Dot(const Vec4& that) const noexcept
    {
        return x * that.x + y * that.y + z * that.z + w * that.w;
    }

    constexpr Vec4 Lerp(const Vec4& that, T t) const noexcept
    {
        return Vec4(*this * (1 - t) + that * t);
    }

    constexpr friend bool operator==(const Vec4& a, const Vec4& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    constexpr friend Vec4 operator+(const Vec4& a, const Vec4& b) noexcept
    {
        return Vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    }

    constexpr friend Vec4 operator-(const Vec4& a, const Vec4& b) noexcept
    {
        return Vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    }

    constexpr friend Vec4 operator*(const Vec4& a, const Vec4& b) noexcept
    {
        return Vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
    }

    constexpr friend Vec4 operator*(const Vec4& v, const T scalar) noexcept
    {
        return Vec4(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
    }

    constexpr friend Vec4 operator*(const T scalar, const Vec4& v) noexcept
    {
        return Vec4(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
    }

    constexpr friend Vec4 operator/(const Vec4& v, const T scalar) noexcept
    {
        MLG_ASSERT(scalar != T{ 0 }, "Division by zero in Vec4 operator/");
        const T invScalar = T{1} / scalar;
        return Vec4(v.x * invScalar, v.y * invScalar, v.z * invScalar, v.w * invScalar);
    }

    constexpr friend Vec4 operator/(const T scalar, const Vec4& v) noexcept
    {
        MLG_ASSERT(v.x != T{ 0 } && v.y != T{ 0 } && v.z != T{ 0 } && v.w != T{ 0 },
            "Division by zero in Vec4 operator/");
        return Vec4(scalar / v.x, scalar / v.y, scalar / v.z, scalar / v.w);
    }

    constexpr friend Vec4 operator-(const Vec4& v) noexcept
    {
        return Vec4(-v.x, -v.y, -v.z, -v.w);
    }

    constexpr T& operator[](size_t index) noexcept
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

    constexpr const T& operator[](size_t index) const noexcept
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

    constexpr Vec4& operator+=(const Vec4& that) noexcept
    {
        return *this = *this + that;
    }

    constexpr Vec4& operator-=(const Vec4& that) noexcept
    {
        return *this = *this - that;
    }

    constexpr Vec4& operator*=(const Vec4& that) noexcept
    {
        return *this = *this * that;
    }

    constexpr Vec4& operator*=(const T scalar) noexcept
    {
        return *this = *this * scalar;
    }

    constexpr Vec4& operator/=(const T scalar) noexcept
    {
        return *this = *this / scalar;
    }
};

template<typename T>
inline constexpr Vec2<T>::Vec2(const Vec3<T>& v) noexcept
    : x(v.x), y(v.y)
{
}

template<typename T>
inline constexpr Vec2<T>::Vec2(const Vec4<T>& v) noexcept
    : x(v.x), y(v.y)
{
}

template<typename T>
inline constexpr Vec3<T>::Vec3(const Vec4<T>& v) noexcept
    : x(v.x), y(v.y), z(v.z)
{
}

template<typename T>
class UnitQuat
{
    static_assert(std::is_floating_point_v<T>, "Quat requires a floating-point type");

public:

    using ValueType = T;

    constexpr UnitQuat() = default;

    constexpr UnitQuat(T x, T y, T z, T w) noexcept
        : UnitQuat(Vec4<T>(x, y, z, w))
    {
    }

    constexpr explicit UnitQuat(const Vec4<T>& v) noexcept
        : m_Vec(v.Normalize())
    {
    }

    constexpr UnitQuat(const Radians<T> angle, const Vec3<T>& axis) noexcept
    {
        const Radians<T> ao2 = angle * T{0.5};
        const T s = ao2.Sin();
        m_Vec.x = axis.x * s;
        m_Vec.y = axis.y * s;
        m_Vec.z = axis.z * s;
        m_Vec.w = ao2.Cos();
    }

    constexpr Mat44<T> ToMatrix() const noexcept;

    constexpr UnitQuat Conjugate() const noexcept
    {
        return UnitQuat(-m_Vec.x, -m_Vec.y, -m_Vec.z, m_Vec.w);
    }

    constexpr UnitQuat Inverse() const noexcept
    {
        return Conjugate();
    }

    constexpr UnitQuat Lerp(const UnitQuat& that, T t) const noexcept
    {
        return UnitQuat(m_Vec.Lerp(that.m_Vec, t));
    }

    constexpr Vec4<T> ToVector() const noexcept
    {
        return m_Vec;
    }

    constexpr friend bool operator==(const UnitQuat& a, const UnitQuat& b) noexcept
    {
        return a.m_Vec == b.m_Vec;
    }

    constexpr friend Vec3<T> operator*(const UnitQuat& q, const Vec3<T>& vec) noexcept
    {
        // Assumes a unit quaternion.
        const Vec3<T> u{ q.m_Vec };
        const Vec3<T> t = u.Cross(vec) * T{2};
        return vec + q.m_Vec.w * t + u.Cross(t);
    }

    constexpr friend UnitQuat operator*(const UnitQuat& a, const UnitQuat& b) noexcept
    {
        return UnitQuat(
            a.m_Vec.w * b.m_Vec.x + a.m_Vec.x * b.m_Vec.w + a.m_Vec.y * b.m_Vec.z - a.m_Vec.z * b.m_Vec.y,
            a.m_Vec.w * b.m_Vec.y - a.m_Vec.x * b.m_Vec.z + a.m_Vec.y * b.m_Vec.w + a.m_Vec.z * b.m_Vec.x,
            a.m_Vec.w * b.m_Vec.z + a.m_Vec.x * b.m_Vec.y - a.m_Vec.y * b.m_Vec.x + a.m_Vec.z * b.m_Vec.w,
            a.m_Vec.w * b.m_Vec.w - a.m_Vec.x * b.m_Vec.x - a.m_Vec.y * b.m_Vec.y - a.m_Vec.z * b.m_Vec.z
        );
    }

    constexpr friend UnitQuat operator+(const UnitQuat& a, const UnitQuat& b) noexcept
    {
        return UnitQuat(a.m_Vec + b.m_Vec);
    }

    constexpr friend UnitQuat operator-(const UnitQuat& a) noexcept
    {
        return UnitQuat(-a.m_Vec);
    }

    constexpr UnitQuat& operator*=(const UnitQuat& that) noexcept
    {
        return (*this = *this * that);
    }

    constexpr UnitQuat& operator+=(const UnitQuat& that) noexcept
    {
        return (*this = *this + that);
    }

private:

    Vec4<T> m_Vec;
};

// 4x4 column-major matrix
template<typename T>
class Mat44
{
    static_assert(std::is_floating_point_v<T>, "Mat44 requires a floating-point type");

public:

    using ValueType = T;

    Vec4<T> m[4];

    constexpr Mat44() = default;

    constexpr Mat44(
        const Vec4<T>& col0, const Vec4<T>& col1, const Vec4<T>& col2, const Vec4<T>& col3) noexcept
    {
        m[0] = col0;
        m[1] = col1;
        m[2] = col2;
        m[3] = col3;
    }

    constexpr explicit Mat44(T value) noexcept
        : Mat44(Vec4<T>(value, 0, 0, 0),
                Vec4<T>(0, value, 0, 0),
                Vec4<T>(0, 0, value, 0),
                Vec4<T>(0, 0, 0, value))
    {
    }

    constexpr Mat44(T m00, T m01, T m02, T m03,
          T m10, T m11, T m12, T m13,
          T m20, T m21, T m22, T m23,
          T m30, T m31, T m32, T m33) noexcept
          : Mat44(Vec4<T>(m00, m01, m02, m03),
                 Vec4<T>(m10, m11, m12, m13),
                 Vec4<T>(m20, m21, m22, m23),
                 Vec4<T>(m30, m31, m32, m33))
    {
    }

    constexpr Mat44 Mul(const Mat44& other) const noexcept
    {
        return *this * other;
    }

    constexpr Vec4<T> Mul(const Vec4<T>& vector) const noexcept
    {
        return *this * vector;
    }

    constexpr Vec4<T> Mul(const Vec3<T>& vector) const noexcept
    {
        return *this * vector;
    }

    constexpr friend bool operator==(const Mat44& a, const Mat44& b) noexcept
    {
        return a.m[0] == b.m[0] && a.m[1] == b.m[1] && a.m[2] == b.m[2] && a.m[3] == b.m[3];
    }

    constexpr Mat44& operator*=(const Mat44& that) noexcept
    {
        return *this = *this * that;
    }

    constexpr Mat44<T> Inverse() const noexcept
    {
        constexpr T epsilon = T{1e-8};

        // Math-style names:
        //
        //     aRC = row R, column C
        //
        // Storage:
        //
        //     m[column][row]

        const T a00 = m[0][0];
        const T a01 = m[1][0];
        const T a02 = m[2][0];
        const T a03 = m[3][0];

        const T a10 = m[0][1];
        const T a11 = m[1][1];
        const T a12 = m[2][1];
        const T a13 = m[3][1];

        const T a20 = m[0][2];
        const T a21 = m[1][2];
        const T a22 = m[2][2];
        const T a23 = m[3][2];

        const T a30 = m[0][3];
        const T a31 = m[1][3];
        const T a32 = m[2][3];
        const T a33 = m[3][3];

        // 2x2 determinants reused by the 4x4 cofactors.
        const T s0 = a00 * a11 - a10 * a01;
        const T s1 = a00 * a12 - a10 * a02;
        const T s2 = a00 * a13 - a10 * a03;
        const T s3 = a01 * a12 - a11 * a02;
        const T s4 = a01 * a13 - a11 * a03;
        const T s5 = a02 * a13 - a12 * a03;

        const T c0 = a20 * a31 - a30 * a21;
        const T c1 = a20 * a32 - a30 * a22;
        const T c2 = a20 * a33 - a30 * a23;
        const T c3 = a21 * a32 - a31 * a22;
        const T c4 = a21 * a33 - a31 * a23;
        const T c5 = a22 * a33 - a32 * a23;

        const T det =
            s0 * c5 -
            s1 * c4 +
            s2 * c3 +
            s3 * c2 -
            s4 * c1 +
            s5 * c0;

        if (std::abs(det) <= epsilon)
        {
            return Mat44<T>{0};
        }

        const T invDet = 1 / det;

        // Math matrix inverse entries, named row/column.
        const T b00 = ( a11 * c5 - a12 * c4 + a13 * c3) * invDet;
        const T b01 = (-a01 * c5 + a02 * c4 - a03 * c3) * invDet;
        const T b02 = ( a31 * s5 - a32 * s4 + a33 * s3) * invDet;
        const T b03 = (-a21 * s5 + a22 * s4 - a23 * s3) * invDet;

        const T b10 = (-a10 * c5 + a12 * c2 - a13 * c1) * invDet;
        const T b11 = ( a00 * c5 - a02 * c2 + a03 * c1) * invDet;
        const T b12 = (-a30 * s5 + a32 * s2 - a33 * s1) * invDet;
        const T b13 = ( a20 * s5 - a22 * s2 + a23 * s1) * invDet;

        const T b20 = ( a10 * c4 - a11 * c2 + a13 * c0) * invDet;
        const T b21 = (-a00 * c4 + a01 * c2 - a03 * c0) * invDet;
        const T b22 = ( a30 * s4 - a31 * s2 + a33 * s0) * invDet;
        const T b23 = (-a20 * s4 + a21 * s2 - a23 * s0) * invDet;

        const T b30 = (-a10 * c3 + a11 * c1 - a12 * c0) * invDet;
        const T b31 = ( a00 * c3 - a01 * c1 + a02 * c0) * invDet;
        const T b32 = (-a30 * s3 + a31 * s1 - a32 * s0) * invDet;
        const T b33 = ( a20 * s3 - a21 * s1 + a22 * s0) * invDet;

        return Mat44<T>
        {
            Vec4<T>(b00, b10, b20, b30),
            Vec4<T>(b01, b11, b21, b31),
            Vec4<T>(b02, b12, b22, b32),
            Vec4<T>(b03, b13, b23, b33),
        };
    }

    constexpr Mat44 InverseAffine() const noexcept
    {
        constexpr T epsilon = T{1e-8};

        // Assumes column-vector convention and storage as:
        //
        //     m[column][row]
        //
        // Affine matrix layout:
        //
        //     [ a00 a01 a02 tx ]
        //     [ a10 a11 a12 ty ]
        //     [ a20 a21 a22 tz ]
        //     [  0   0   0  1 ]

        const T a00 = m[0][0];
        const T a01 = m[1][0];
        const T a02 = m[2][0];

        const T a10 = m[0][1];
        const T a11 = m[1][1];
        const T a12 = m[2][1];

        const T a20 = m[0][2];
        const T a21 = m[1][2];
        const T a22 = m[2][2];

        const T tx = m[3][0];
        const T ty = m[3][1];
        const T tz = m[3][2];

        const T c00 = a11 * a22 - a12 * a21;
        const T c01 = a02 * a21 - a01 * a22;
        const T c02 = a01 * a12 - a02 * a11;

        const T c10 = a12 * a20 - a10 * a22;
        const T c11 = a00 * a22 - a02 * a20;
        const T c12 = a02 * a10 - a00 * a12;

        const T c20 = a10 * a21 - a11 * a20;
        const T c21 = a01 * a20 - a00 * a21;
        const T c22 = a00 * a11 - a01 * a10;

        const T det = a00 * c00 + a01 * c10 + a02 * c20;

        if (std::abs(det) <= epsilon)
        {
            return Mat44(0);
        }

        const T invDet = 1 / det;

        const T b00 = c00 * invDet;
        const T b01 = c01 * invDet;
        const T b02 = c02 * invDet;

        const T b10 = c10 * invDet;
        const T b11 = c11 * invDet;
        const T b12 = c12 * invDet;

        const T b20 = c20 * invDet;
        const T b21 = c21 * invDet;
        const T b22 = c22 * invDet;

        const T itx = -(b00 * tx + b01 * ty + b02 * tz);
        const T ity = -(b10 * tx + b11 * ty + b12 * tz);
        const T itz = -(b20 * tx + b21 * ty + b22 * tz);

        return Mat44
        {
            Vec4<T>(b00, b10, b20, 0),
            Vec4<T>(b01, b11, b21, 0),
            Vec4<T>(b02, b12, b22, 0),
            Vec4<T>(itx, ity, itz, 1),
        };
    }

    constexpr Mat44 Transpose() const noexcept
    {
        return Mat44 //
            {
                Vec4<T>(m[0].x, m[1].x, m[2].x, m[3].x),
                Vec4<T>(m[0].y, m[1].y, m[2].y, m[3].y),
                Vec4<T>(m[0].z, m[1].z, m[2].z, m[3].z),
                Vec4<T>(m[0].w, m[1].w, m[2].w, m[3].w),
            };
    }

    constexpr void Decompose(Vec3<T>& translation, UnitQuat<T>& rotation, Vec3<T>& scale) const noexcept
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

        rotation = UnitQuat<T>(qx, qy, qz, qw);
    }

    constexpr Vec4<T>& operator[](std::size_t index) noexcept
    {
        return m[index];
    }

    constexpr const Vec4<T>& operator[](std::size_t index) const noexcept
    {
        return m[index];
    }

    constexpr friend Mat44 operator*(const Mat44& a, const Mat44& b) noexcept
    {
        return Mat44 //
            { a * b[0], a * b[1], a * b[2], a * b[3] };
    }

    constexpr friend Vec4<T> operator*(const Mat44& mat, const Vec4<T>& vector) noexcept
    {
        return mat.m[0] * vector.x + mat.m[1] * vector.y + mat.m[2] * vector.z + mat.m[3] * vector.w;
    }

    constexpr friend Vec4<T> operator*(const Mat44& mat, const Vec3<T>& vector) noexcept
    {
        return Vec4<T>(mat.m[0] * vector.x + mat.m[1] * vector.y + mat.m[2] * vector.z + mat.m[3]);
    }

    static constexpr const Mat44& Identity() noexcept
    {
        static constexpr Mat44 IDENT(1);

        return IDENT;
    }

    static Mat44 PerspectiveLH(
        const Radians<T> fov, const T aspectRatio, const T nearClip, const T farClip) noexcept
    {
        const T t = (fov * T{0.5}).Tan();

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
inline constexpr Mat44<T> UnitQuat<T>::ToMatrix() const noexcept
{
    const T xx = m_Vec.x * m_Vec.x;
    const T yy = m_Vec.y * m_Vec.y;
    const T zz = m_Vec.z * m_Vec.z;
    const T xy = m_Vec.x * m_Vec.y;
    const T xz = m_Vec.x * m_Vec.z;
    const T yz = m_Vec.y * m_Vec.z;
    const T wx = m_Vec.w * m_Vec.x;
    const T wy = m_Vec.w * m_Vec.y;
    const T wz = m_Vec.w * m_Vec.z;

    // Column-major 4x4 rotation matrix
    return Mat44<T> //
        {
            Vec4<T>(1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0),

            Vec4<T>(2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0),

            Vec4<T>(2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0),

            Vec4<T>(0,0,0,1),
        };
}

template<typename NumType>
class TrsTransform
{
    static_assert(std::is_floating_point_v<NumType>, "TrsTransform requires a floating-point type");

public:

    using ValueType = NumType;

    Vec3<NumType> T{ 0 };
    UnitQuat<NumType> R{ Radians<NumType>{0}, Vec3<NumType>{0,1,0} };
    Vec3<NumType> S{ 1 };

    constexpr Mat44<NumType> ToMatrix() const noexcept
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
    constexpr Mat44<NumType> Inverse() const noexcept
    {
        TrsTransform result;
        result.R = R.Inverse();
        result.S = NumType{1} / S;
        result.T = -(result.R * (T * result.S));
        return result.ToMatrix();
    }

    static TrsTransform FromMatrix(const Mat44<NumType>& mat) noexcept
    {
        TrsTransform result;
        mat.Decompose(result.T, result.R, result.S);
        return result;
    }

    constexpr Vec3<NumType> LocalXAxis() const noexcept
    {
        return (R * Vec3<NumType>::XAXIS()).Normalize();
    }

    constexpr Vec3<NumType> LocalYAxis() const noexcept
    {
        return (R * Vec3<NumType>::YAXIS()).Normalize();
    }

    constexpr Vec3<NumType> LocalZAxis() const noexcept
    {
        return (R * Vec3<NumType>::ZAXIS()).Normalize();
    }

    constexpr friend Vec3<NumType> operator*(const TrsTransform<NumType>& transform,
        const Vec3<NumType>& point) noexcept
    {
        return transform.R * (point * transform.S) + transform.T;
    }

    constexpr friend bool operator==(const TrsTransform<NumType>& a,
        const TrsTransform<NumType>& b) noexcept
    {
        return a.T == b.T && a.R == b.R && a.S == b.S;
    }
};

class Extent
{
public:

    float Width;
    float Height;

    constexpr friend bool operator==(const Extent& a, const Extent& b) noexcept
    {
        return a.Width == b.Width && a.Height == b.Height;
    }
};

class Point
{
public:

    float X;
    float Y;

    constexpr friend bool operator==(const Point& a, const Point& b) noexcept
    {
        return a.X == b.X && a.Y == b.Y;
    }
};

using Radiansf = Radians<float>;
using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using UnitQuatf = UnitQuat<float>;
using Mat44f = Mat44<float>;
using TrsTransformf = TrsTransform<float>;

// NOLINTBEGIN(bugprone-std-namespace-modification): std::formatter is a standard customization point for user-defined types.
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
struct std::formatter<UnitQuat<T>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const UnitQuat<T>& quat, FormatContext& ctx) const
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
// NOLINTEND(bugprone-std-namespace-modification)
