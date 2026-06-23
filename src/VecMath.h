#pragma once

#include "AssertHelper.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <cstddef>

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
        const T r = value - (std::floor(value / TWO_PI) * TWO_PI);

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

    constexpr explicit Vec2(const T(&arr)[2]) noexcept
        : x(arr[0]), y(arr[1])
    {
    }

    constexpr explicit Vec2(const Vec3<T>& v) noexcept;

    constexpr explicit Vec2(const Vec4<T>& v) noexcept;

    [[nodiscard]] constexpr Vec2<T> Normalize() const noexcept
    {
        return *this / Length();
    }

    constexpr T Length() const noexcept
    {
        return std::sqrt((x * x) + (y * y));
    }

    constexpr T Length2() const noexcept

    {
        return (x * x) + (y * y);
    }

    [[nodiscard]] constexpr Vec2 Cross(const Vec2& that) const noexcept
    {
        return Vec2((x * that.y) - (y * that.x), (y * that.x) - (x * that.y));
    }

    constexpr T Dot(const Vec2& that) const noexcept
    {
        return (x * that.x) + (y * that.y);
    }

    [[nodiscard]] constexpr Vec2 Lerp(const Vec2& that, T t) const noexcept
    {
        return Vec2((*this * (1 - t)) + (that * t));
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

    constexpr explicit Vec3(const T(&arr)[3]) noexcept
        : x(arr[0]), y(arr[1]), z(arr[2])
    {
    }

    constexpr Vec3(const Vec2<T>& v, const T inZ) noexcept
        : x(v.x), y(v.y), z(inZ)
    {
    }

    constexpr explicit Vec3(const Vec4<T>& v) noexcept;

    static consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    [[nodiscard]] constexpr Vec3 Normalize() const noexcept
    {
        return *this / Length();
    }

    constexpr T Length() const noexcept
    {
        return std::sqrt((x * x) + (y * y) + (z * z));
    }

    constexpr T Length2() const noexcept
    {
        return (x * x) + (y * y) + (z * z);
    }

    [[nodiscard]] constexpr Vec3 Cross(const Vec3& that) const noexcept
    {
        return Vec3(
            (y * that.z) - (z * that.y),
            (z * that.x) - (x * that.z),
            (x * that.y) - (y * that.x)
        );
    }

    constexpr T Dot(const Vec3& that) const noexcept
    {
        return (x * that.x) + (y * that.y) + (z * that.z);
    }

    [[nodiscard]] constexpr Vec3 Lerp(const Vec3& that, T t) const noexcept
    {
        return Vec3((*this * (1 - t)) + (that * t));
    }

    /// Rotates this vector around the given axis by the specified angle.
    [[nodiscard]] constexpr Vec3 RotateBy(const Vec3& axis, const Radians<T> angle) const noexcept
    {
        // https://en.wikipedia.org/wiki/Rodrigues'_rotation_formula
        const T cos = angle.Cos();
        const T sin = angle.Sin();
        const Vec3 vr = (*this * cos) + (axis.Cross(*this) * sin) +
                         (axis * (axis.Dot(*this)) * (1 - cos));
        return vr;
    }

    constexpr Vec2<T> xy() const noexcept
    {
        return Vec2<T>(x, y);
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

    constexpr explicit Vec4(const T(&arr)[4]) noexcept
        : x(arr[0]), y(arr[1]), z(arr[2]), w(arr[3])
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

    [[nodiscard]] constexpr Vec4 Normalize() const noexcept
    {
        return *this / Length();
    }

    constexpr T Length() const noexcept
    {
        return std::sqrt((x * x) + (y * y) + (z * z) + (w * w));
    }

    constexpr T Length2() const noexcept
    {
        return (x * x) + (y * y) + (z * z) + (w * w);
    }

    constexpr T Dot(const Vec4& that) const noexcept
    {
        return (x * that.x) + (y * that.y) + (z * that.z) + (w * that.w);
    }

    [[nodiscard]] constexpr Vec4 Lerp(const Vec4& that, T t) const noexcept
    {
        return Vec4((*this * (1 - t)) + (that * t));
    }

    constexpr Vec2<T> xy() const noexcept
    {
        return Vec2<T>(x, y);
    }

    constexpr Vec3<T> xyz() const noexcept
    {
        return Vec3<T>(x, y, z);
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
constexpr Vec2<T>::Vec2(const Vec3<T>& v) noexcept
    : x(v.x), y(v.y)
{
}

template<typename T>
constexpr Vec2<T>::Vec2(const Vec4<T>& v) noexcept
    : x(v.x), y(v.y)
{
}

template<typename T>
constexpr Vec3<T>::Vec3(const Vec4<T>& v) noexcept
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

    constexpr explicit UnitQuat(const T(&arr)[4]) noexcept
        : UnitQuat(Vec4<T>(arr[0], arr[1], arr[2], arr[3]))
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

    Mat44<T> ToMatrix() const noexcept;

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
        return vec + (q.m_Vec.w * t) + u.Cross(t);
    }

    constexpr friend UnitQuat operator*(const UnitQuat& a, const UnitQuat& b) noexcept
    {
        return UnitQuat(
            (a.m_Vec.w * b.m_Vec.x) + (a.m_Vec.x * b.m_Vec.w) + (a.m_Vec.y * b.m_Vec.z) - (a.m_Vec.z * b.m_Vec.y),
            (a.m_Vec.w * b.m_Vec.y) - (a.m_Vec.x * b.m_Vec.z) + (a.m_Vec.y * b.m_Vec.w) + (a.m_Vec.z * b.m_Vec.x),
            (a.m_Vec.w * b.m_Vec.z) + (a.m_Vec.x * b.m_Vec.y) - (a.m_Vec.y * b.m_Vec.x) + (a.m_Vec.z * b.m_Vec.w),
            (a.m_Vec.w * b.m_Vec.w) - (a.m_Vec.x * b.m_Vec.x) - (a.m_Vec.y * b.m_Vec.y) - (a.m_Vec.z * b.m_Vec.z)
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

    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    constexpr explicit Mat44(const T(&arr)[16]) noexcept
        : Mat44(Vec4<T>(arr[0], arr[1], arr[2], arr[3]),
                Vec4<T>(arr[4], arr[5], arr[6], arr[7]),
                Vec4<T>(arr[8], arr[9], arr[10], arr[11]),
                Vec4<T>(arr[12], arr[13], arr[14], arr[15]))
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers))
    {
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

    [[nodiscard]] constexpr Mat44 Mul(const Mat44& other) const noexcept
    {
        return *this * other;
    }

    [[nodiscard]] constexpr Vec4<T> Mul(const Vec4<T>& vector) const noexcept
    {
        return *this * vector;
    }

    [[nodiscard]] constexpr Vec4<T> Mul(const Vec3<T>& vector) const noexcept
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

    [[nodiscard]] Mat44<T> Inverse() const noexcept;

    [[nodiscard]] Mat44 InverseAffine() const noexcept;

    [[nodiscard]] constexpr Mat44 Transpose() const noexcept
    {
        return Mat44 //
            {
                Vec4<T>(m[0].x, m[1].x, m[2].x, m[3].x),
                Vec4<T>(m[0].y, m[1].y, m[2].y, m[3].y),
                Vec4<T>(m[0].z, m[1].z, m[2].z, m[3].z),
                Vec4<T>(m[0].w, m[1].w, m[2].w, m[3].w),
            };
    }

    void Decompose(Vec3<T>& translation, UnitQuat<T>& rotation, Vec3<T>& scale) const noexcept;

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
        return Mat44{ a * b[0], a * b[1], a * b[2], a * b[3] };
    }

    constexpr friend Vec4<T> operator*(const Mat44& mat, const Vec4<T>& vector) noexcept
    {
        return (mat.m[0] * vector.x) + (mat.m[1] * vector.y) + (mat.m[2] * vector.z) + (mat.m[3] * vector.w);
    }

    constexpr friend Vec4<T> operator*(const Mat44& mat, const Vec3<T>& vector) noexcept
    {
        return (mat.m[0] * vector.x) + (mat.m[1] * vector.y) + (mat.m[2] * vector.z) + mat.m[3];
    }

    static constexpr const Mat44& Identity() noexcept
    {
        static constexpr Mat44 IDENT(1);

        return IDENT;
    }

    static Mat44 PerspectiveLH(
        const Radians<T> fov, const T aspectRatio, const T nearClip, const T farClip) noexcept;
};

template<typename NumType>
class Pose
{
    static_assert(std::is_floating_point_v<NumType>, "Pose requires a floating-point type");

public:

    using ValueType = NumType;

    Vec3<NumType> T{ 0 };
    UnitQuat<NumType> R{ Radians<NumType>{0}, Vec3<NumType>{0,1,0} };

    Mat44<NumType> ToMatrix() const noexcept;

    [[nodiscard]] Pose<NumType> Inverse() const noexcept;

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

    constexpr friend Vec3<NumType> operator*(const Pose<NumType>& pose,
        const Vec3<NumType>& point) noexcept
    {
        return (pose.R * point) + pose.T;
    }

    constexpr friend bool operator==(const Pose<NumType>& a, const Pose<NumType>& b) noexcept
    {
        return a.T == b.T && a.R == b.R;
    }
};

template<typename NumType>
class TrsTransform
{
    static_assert(std::is_floating_point_v<NumType>, "TrsTransform requires a floating-point type");

public:

    using ValueType = NumType;

    Vec3<NumType> T{ 0 };
    UnitQuat<NumType> R{ Radians<NumType>{0}, Vec3<NumType>{0,1,0} };
    Vec3<NumType> S{ 1 };

    static TrsTransform FromMatrix(const Mat44<NumType>& mat) noexcept;

    Mat44<NumType> ToMatrix() const noexcept;

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
        return (transform.R * (point * transform.S)) + transform.T;
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

    unsigned Width;
    unsigned Height;

    constexpr float GetAspectRatio() const
    {
        MLG_ASSERT(Height != 0, "Height must be non-zero to compute aspect ratio");
        return static_cast<float>(Width) / static_cast<float>(Height);
    }

    constexpr friend bool operator==(const Extent& a, const Extent& b) noexcept
    {
        return a.Width == b.Width && a.Height == b.Height;
    }
};

class Point
{
public:

    int X{0}, Y{0};

    constexpr friend bool operator==(const Point& a, const Point& b) noexcept
    {
        return a.X == b.X && a.Y == b.Y;
    }
};

class Rect
{
public:
    Rect() = delete;

    struct RectParams
    {
        int X, Y;
        unsigned Width, Height;
    };

    explicit Rect(const RectParams& params) noexcept
        : m_X(params.X), m_Y(params.Y), m_Width(params.Width), m_Height(params.Height)
    {
        if(!MLG_VERIFY(m_Width > 0, "Width must be non-zero"))
        {
            m_Width = 1;
        }

        if(!MLG_VERIFY(m_Height > 0, "Height must be non-zero"))
        {
            m_Height = 1;
        }
    }

    int GetX() const { return m_X; }
    int GetY() const { return m_Y; }
    unsigned GetWidth() const { return m_Width; }
    unsigned GetHeight() const { return m_Height; }

    constexpr Extent GetExtent() const { return Extent{.Width = m_Width, .Height = m_Height }; }

    constexpr float GetAspectRatio() const
    {
        return GetExtent().GetAspectRatio();
    }

    constexpr friend bool operator==(const Rect& a, const Rect& b) noexcept
    {
        return a.m_X == b.m_X && a.m_Y == b.m_Y && a.m_Width == b.m_Width && a.m_Height == b.m_Height;
    }

private:
    int m_X{0}, m_Y{0};
    unsigned m_Width{0}, m_Height{0};
};

using Radiansf = Radians<float>;
using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using UnitQuatf = UnitQuat<float>;
using Mat44f = Mat44<float>;
using TrsTransformf = TrsTransform<float>;
using Posef = Pose<float>;