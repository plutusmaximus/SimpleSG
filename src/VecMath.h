#pragma once

#include <numbers>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

    constexpr Degrees<T> operator*(const T other) const
    {
        return Degrees<T>(m_Value * other);
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

    constexpr Degrees<T> operator*=(const T other)
    {
        m_Value *= other;
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

    constexpr Radians<T> operator*=(const T other)
    {
        m_Value *= other;
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
inline constexpr Degrees<T> operator*(const T a, const Degrees<T> b)
{
    return b * a;
}

template<typename T>
inline constexpr Radians<T> operator*(const T a, const Radians<T> b)
{
    return b * a;
}

template<typename T>
class Vec2 : public glm::vec<2, T>
{
    using Base = glm::vec<2, T>;
public:

    constexpr Vec2(const Base& v)
        : Base(v)
    {
    }

    using Base::vec;
    using Base::x;
    using Base::y;

    Vec2 Normalize() const
    {
        return glm::normalize(*this);
    }

    constexpr Vec2 Cross(const Vec2& that) const
    {
        return glm::cross(*this, that);
    }

    constexpr T Dot(const Vec2& that) const
    {
        // This static_cast is needed because glm defines:
        // T dot(T x, T y)
        // And without the static cast calling dot() incorrectly resolves
        // to that overload.
        return glm::dot(static_cast<const Base&>(*this), that);
    }

    constexpr Vec2 operator+(const Vec2& that) const
    {
        return glm::operator+(*this, that);
    }

    constexpr Vec2 operator-(const Vec2& that) const
    {
        return glm::operator-(*this, that);
    }

    constexpr Vec2 operator*(const Vec2& that) const
    {
        return glm::operator*(*this, that);
    }

    Vec2& operator+=(const Vec2& that)
    {
        this->Base::operator+=(static_cast<const Base&>(that));
        return *this;
    }

    Vec2& operator-=(const Vec2& that)
    {
        this->Base::operator-=(static_cast<const Base&>(that));
        return *this;
    }

    Vec2& operator*=(const Vec2& that)
    {
        this->Base::operator*=(static_cast<const Base&>(that));
        return *this;
    }
};

template<typename T>
class Vec3 : public glm::vec<3, T>
{
    using Base = glm::vec<3, T>;

public:

    constexpr Vec3(const Base& v)
        : Base(v)
    {
    }

    using Base::vec;
    using Base::x;
    using Base::y;
    using Base::z;

    static inline consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static inline consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static inline consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    Vec3 Normalize() const
    {
        return glm::normalize(*this);
    }
    
    constexpr Vec3 Cross(const Vec3& that) const
    {
        return glm::cross(*this, that);
    }

    constexpr T Dot(const Vec3& that) const
    {
        // This static_cast is needed because glm defines:
        // T dot(T x, T y)
        // And without the static cast calling dot() incorrectly resolves
        // to that overload.
        return glm::dot(static_cast<const Base&>(*this), that);
    }

    constexpr Vec3 operator+(const Vec3& that) const
    {
        return glm::operator+(*this, that);
    }

    constexpr Vec3 operator-(const Vec3& that) const
    {
        return glm::operator-(*this, that);
    }

    constexpr Vec3 operator*(const Vec3& that) const
    {
        return glm::operator*(*this, that);
    }

    Vec3& operator+=(const Vec3& that)
    {
        this->Base::operator+=(static_cast<const Base&>(that));
        return *this;
    }

    Vec3& operator-=(const Vec3& that)
    {
        this->Base::operator-=(static_cast<const Base&>(that));
        return *this;
    }

    Vec3& operator*=(const Vec3& that)
    {
        this->Base::operator*=(static_cast<const Base&>(that));
        return *this;
    }
};

template<typename T>
class Vec4 : public glm::vec<4, T>
{
    using Base = glm::vec<4, T>;
public:

    constexpr Vec4(const Base& v)
        : Base(v)
    {
    }

    using Base::vec;
    using Base::x;
    using Base::y;
    using Base::z;
    using Base::w;

    Vec4 Normalize() const
    {
        return glm::normalize(*this);
    }

    constexpr Vec4 Cross(const Vec4& that) const
    {
        return glm::cross(*this, that);
    }

    constexpr T Dot(const Vec4& that) const
    {
        // This static_cast is needed because glm defines:
        // T dot(T x, T y)
        // And without the static cast calling dot() incorrectly resolves
        // to that overload.
        return glm::dot(static_cast<const Base&>(*this), that);
    }

    constexpr Vec4 operator+(const Vec4& that) const
    {
        return glm::operator+(*this, that);
    }

    constexpr Vec4 operator-(const Vec4& that) const
    {
        return glm::operator-(*this, that);
    }

    constexpr Vec4 operator*(const Vec4& that) const
    {
        return glm::operator*(*this, that);
    }

    Vec4& operator+=(const Vec4& that)
    {
        this->Base::operator+=(static_cast<const Base&>(that));
        return *this;
    }

    Vec4& operator-=(const Vec4& that)
    {
        this->Base::operator-=(static_cast<const Base&>(that));
        return *this;
    }

    Vec4& operator*=(const Vec4& that)
    {
        this->Base::operator*=(static_cast<const Base&>(that));
        return *this;
    }
};

template<typename T>
class Quat : public glm::qua<T, glm::qualifier::defaultp>
{
    using Base = glm::qua<T, glm::qualifier::defaultp>;

public:

    constexpr Quat(const Base& q)
        : Base(q)
    {
    }

    Quat(const Degrees<T> angle, const Vec3<T>& axis)
        : Quat(angle.ToRadians(), axis)
    {
    }

    Quat(const Radians<T> angle, const Vec3<T>& axis)
        : Base(glm::angleAxis(angle.Value(), axis))
    {
    }

    using Base::x;
    using Base::y;
    using Base::z;
    using Base::w;

    Quat Normalize() const
    {
        return glm::normalize(*this);
    }

    constexpr Quat operator*(const Quat& that) const
    {
        return Quat(glm::operator*(*this, that)).Normalize();
    }

    Quat& operator*=(const Quat& that)
    {
        return (*this = *this * that);
    }
};

// 4x4 column-major matrix
template<typename T>
class Mat44 : public glm::mat<4, 4, T>
{
    using Base = glm::mat<4, 4, T>;
public:

    constexpr Mat44(const Base& m)
        : Base(m)
    {
    }

    using Base::mat;

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

    Mat44& operator*=(const Mat44& that)
    {
        this->Base::operator*=(static_cast<const Base&>(that));
        return *this;
    }

    Mat44 Inverse() const
    {
        return glm::inverse(*this);
    }

    Mat44 Transpose() const
    {
        return glm::transpose(*this);
    }

    static constexpr const Mat44& Identity()
    {
        static constexpr Mat44 IDENT = Base(1);

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

template<typename NumType>

class TrsTransform
{
public:

    Vec3<NumType> T{ 0 };
    Quat<NumType> R{ Radians<NumType>(0), Vec3<NumType>{0,1,0} };
    Vec3<NumType> S{ 1 };

    Mat44<NumType> ToMatrix() const
    {
        glm::mat4 M = glm::mat4_cast(R);
        M[0] *= S.x;
        M[1] *= S.y;
        M[2] *= S.z;
        M[3][0] = T.x;
        M[3][1] = T.y;
        M[3][2] = T.z;
        return M;
    }
};

using Degreesf = Degrees<float>;
using Radiansf = Radians<float>;
using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Quatf = Quat<float>;
using Mat44f = Mat44<float>;
using TrsTransformf = TrsTransform<float>;