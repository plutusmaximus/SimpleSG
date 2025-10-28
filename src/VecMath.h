#pragma once

#include <cmath>
#include <numbers>

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
class Vec3
{
public:

    T x, y, z;

    static inline consteval Vec3 XAXIS() { return Vec3{ 1, 0, 0 }; }
    static inline consteval Vec3 YAXIS() { return Vec3{ 0, 1, 0 }; }
    static inline consteval Vec3 ZAXIS() { return Vec3{ 0, 0, 1 }; }

    T& operator[](int index)
    {
        return (&x)[index];
    }

    const T& operator[](int index) const
    {
        return (&x)[index];
    }

    Vec3 Normalize() const
    {
        const T s = (x * x) + (y * y) + (z * z);
        if (s > 0)
        {
            const T len = std::sqrt(s);

            return Vec3(x / len, y / len, z / len);
        }

        return Vec3(0, 0, 0);
    }

    constexpr Vec3 Scale(const T scale)
    {
        return Vec3{ x * scale, y * scale, z * scale };
    }
    
    constexpr Vec3 Cross(const Vec3& that) const noexcept
    {
        return Vec3(
            z * that.y - y * that.z,
            x * that.z - z * that.x,
            y * that.x - x * that.y);
    }


    constexpr T Dot(const Vec3& that) const
    {
        return x * that.x + y * that.y + z * that.z;
    }

    constexpr Vec3 operator+(const Vec3& that) const
    {
        return Vec3(x + that.x, y + that.y, z + that.z);
    }

    constexpr Vec3 operator-(const Vec3& that) const
    {
        return Vec3(x - that.x, y - that.y, z - that.z);
    }
};

template<typename T>
class Vec4
{
public:
    T x, y, z, w;

    T& operator[](int index)
    {
        return (&x)[index];
    }

    const T& operator[](int index) const
    {
        return (&x)[index];
    }

    Vec4 Normalize() const
    {
        const T s = (x * x) + (y * y) + (z * z) + (w * w);
        if (s > 0)
        {
            const T len = std::sqrt(s);

            return Vec4(x / len, y / len, z / len, w / len);
        }

        return Vec4(0, 0, 0, 0);
    }

    constexpr Vec4 Scale(const T scale)
    {
        return Vec4{ x * scale, y * scale, z * scale, w * scale };
    }
};

template<typename T>
class Quaternion
{
public:
    T x, y, z, w;

    Quaternion() = default;

    Quaternion(Radians<T> rad, const Vec3<T>& axis)
    {
        // Normalize the axis
        const Vec3 norm = axis.Normalize();

        T halfAngle = rad.Value() * 0.5f;
        T s = std::sin(halfAngle);

        x = norm.x * s;
        y = norm.y * s;
        z = norm.z * s;
        w = std::cos(halfAngle);
    }

    T& operator[](int index)
    {
        return (&x)[index];
    }

    const T& operator[](int index) const
    {
        return (&x)[index];
    }

    Quaternion Mul(const Quaternion& other) const
    {
        Quaternion result;

        result.x = w * other.x + x * other.w + y * other.z - z * other.y; // x
        result.y = w * other.y - x * other.z + y * other.w + z * other.x; // y
        result.z = w * other.z + x * other.y - y * other.x + z * other.w; // z
        result.w = w * other.w - x * other.x - y * other.y - z * other.z; // w

        return result;
    }

    constexpr Quaternion operator-() const
    {
        Quaternion result;
        result.x = -x, result.y = -y, result.z = -z, result.w = -w;
        return result;
    }
};

// 4x4 column-major matrix
template<typename T>
class Mat44
{
public:

    union
    {
        T mm[4][4];
        struct
        {
            T m00, m01, m02, m03;
            T m10, m11, m12, m13;
            T m20, m21, m22, m23;
            T m30, m31, m32, m33;
        }m;
    };

    Mat44 Mul(const Mat44& other) const
    {
        Mat44 result;

        result.m.m00 = (m.m00 * other.m.m00) + (m.m10 * other.m.m01) + (m.m20 * other.m.m02) + (m.m30 * other.m.m03);
        result.m.m10 = (m.m00 * other.m.m10) + (m.m10 * other.m.m11) + (m.m20 * other.m.m12) + (m.m30 * other.m.m13);
        result.m.m20 = (m.m00 * other.m.m20) + (m.m10 * other.m.m21) + (m.m20 * other.m.m22) + (m.m30 * other.m.m23);
        result.m.m30 = (m.m00 * other.m.m30) + (m.m10 * other.m.m31) + (m.m20 * other.m.m32) + (m.m30 * other.m.m33);

        result.m.m01 = (m.m01 * other.m.m00) + (m.m11 * other.m.m01) + (m.m21 * other.m.m02) + (m.m31 * other.m.m03);
        result.m.m11 = (m.m01 * other.m.m10) + (m.m11 * other.m.m11) + (m.m21 * other.m.m12) + (m.m31 * other.m.m13);
        result.m.m21 = (m.m01 * other.m.m20) + (m.m11 * other.m.m21) + (m.m21 * other.m.m22) + (m.m31 * other.m.m23);
        result.m.m31 = (m.m01 * other.m.m30) + (m.m11 * other.m.m31) + (m.m21 * other.m.m32) + (m.m31 * other.m.m33);

        result.m.m02 = (m.m02 * other.m.m00) + (m.m12 * other.m.m01) + (m.m22 * other.m.m02) + (m.m32 * other.m.m03);
        result.m.m12 = (m.m02 * other.m.m10) + (m.m12 * other.m.m11) + (m.m22 * other.m.m12) + (m.m32 * other.m.m13);
        result.m.m22 = (m.m02 * other.m.m20) + (m.m12 * other.m.m21) + (m.m22 * other.m.m22) + (m.m32 * other.m.m23);
        result.m.m32 = (m.m02 * other.m.m30) + (m.m12 * other.m.m31) + (m.m22 * other.m.m32) + (m.m32 * other.m.m33);

        result.m.m03 = (m.m03 * other.m.m00) + (m.m13 * other.m.m01) + (m.m23 * other.m.m02) + (m.m33 * other.m.m03);
        result.m.m13 = (m.m03 * other.m.m10) + (m.m13 * other.m.m11) + (m.m23 * other.m.m12) + (m.m33 * other.m.m13);
        result.m.m23 = (m.m03 * other.m.m20) + (m.m13 * other.m.m21) + (m.m23 * other.m.m22) + (m.m33 * other.m.m23);
        result.m.m33 = (m.m03 * other.m.m30) + (m.m13 * other.m.m31) + (m.m23 * other.m.m32) + (m.m33 * other.m.m33);

        return result;
    }

    Vec4<T> Mul(const Vec4<T>& vector) const
    {
        Vec4<T> result;

        for (int row = 0; row < 4; ++row)
        {
            result[row] = mm[0][row] * vector[0];
            result[row] += mm[1][row] * vector[1];
            result[row] += mm[2][row] * vector[2];
            result[row] += mm[3][row] * vector[3];
        }

        return result;
    }

    Mat44 Translate(const Vec3<T>& t) const
    {
        return Translate(t.x, t.y, t.z);
    }

    Mat44 Translate(const T tx, const T ty, const T tz) const
    {
        Mat44 result = *this;
        result.mm[0][3] += tx;
        result.mm[1][3] += ty;
        result.mm[2][3] += tz;

        return result;
    }

    Mat44 Rotate(const Quaternion<T>& quat) const
    {

        const T x = quat.x, y = quat.y, z = quat.z, w = quat.w;
        const T x2 = x * x, y2 = y * y, z2 = z * z;
        const T xy = x * y, xz = x * z, yz = y * z;
        const T wx = w * x, wy = w * y, wz = w * z;

        const Mat44 rot
        {
            .mm
            {
                {1 - 2 * (y2 + z2), 2 * (xy - wz), 2 * (xz + wy), 0},
                {2 * (xy + wz), 1 - 2 * (x2 + z2), 2 * (yz - wx), 0},
                {2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (x2 + y2), 0},
                {0, 0, 0, 1},
            }
        };

        return this->Mul(rot);
    }

    Mat44 Rotate(const Degrees<T> deg, const Vec3<T>& axis) const
    {
        return Rotate(deg.ToRadians(), axis);
    }

    Mat44 Rotate(const Radians<T> rad, const Vec3<T>& axis) const
    {
        // Normalize the axis
        const Vec3<T> norm = axis.Normalize();

        const T c = std::cos(rad.Value());
        const T s = std::sin(rad.Value());
        const T t = 1 - c;

        const Mat44 rot
        {
            .mm
            {
                {t * norm.x * norm.x + c, t * norm.x * norm.y - s * norm.z, t * norm.x * norm.z + s * norm.y, 0},
                {t * norm.x * norm.y + s * norm.z, t * norm.y * norm.y + c, t * norm.y * norm.z - s * norm.x, 0},
                {t * norm.x * norm.z - s * norm.y, t * norm.y * norm.z + s * norm.x, t * norm.z * norm.z + c, 0},
                {0, 0, 0, 1}
            }
        };

        return this->Mul(rot);
    }

    Mat44 Scale(const T scalar) const
    {
        const Mat44 scale
        {
            .mm
            {
                {scalar, 0, 0, 0},
                {0, scalar, 0, 0},
                {0, 0, scalar, 0},
                {0, 0, 0, 1},
            }
        };

        return this->Mul(scale);
    }
    
    Mat44 Inverse() const
    {
        // Compute determinant of 3x3 submatrix
        const T det =
            m.m00 * (m.m11 * m.m22 - m.m12 * m.m21) -
            m.m10 * (m.m01 * m.m22 - m.m02 * m.m21) +
            m.m20 * (m.m01 * m.m12 - m.m02 * m.m11);

        if (std::abs(det) < 1e-6)
        {
            return Mat44::Identity(); // Identity for non-invertible matrix
        }

        const T invDet = 1 / det;
        Mat44 result;

        // Inverse of 3x3 submatrix (adjugate method)
        result.mm[0][0] = (m.m11 * m.m22 - m.m12 * m.m21) * invDet;
        result.mm[0][1] = (m.m02 * m.m21 - m.m01 * m.m22) * invDet;
        result.mm[0][2] = (m.m01 * m.m12 - m.m02 * m.m11) * invDet;
        result.mm[0][3] = 0;

        result.mm[1][0] = (m.m12 * m.m20 - m.m10 * m.m22) * invDet;
        result.mm[1][1] = (m.m00 * m.m22 - m.m02 * m.m20) * invDet;
        result.mm[1][2] = (m.m02 * m.m10 - m.m00 * m.m12) * invDet;
        result.mm[1][3] = 0;

        result.mm[2][0] = (m.m10 * m.m21 - m.m11 * m.m20) * invDet;
        result.mm[2][1] = (m.m01 * m.m20 - m.m00 * m.m21) * invDet;
        result.mm[2][2] = (m.m00 * m.m11 - m.m01 * m.m10) * invDet;
        result.mm[2][3] = 0;

        // Translation: -R^-1 * t
        const T tx = mm[3][0], ty = mm[3][1], tz = mm[3][2];
        result.mm[3][0] = -(result.mm[0][0] * tx + result.mm[1][0] * ty + result.mm[2][0] * tz);
        result.mm[3][1] = -(result.mm[0][1] * tx + result.mm[1][1] * ty + result.mm[2][1] * tz);
        result.mm[3][2] = -(result.mm[0][2] * tx + result.mm[1][2] * ty + result.mm[2][2] * tz);
        result.mm[3][3] = 1;

        return result;
    }

    Mat44 Transpose() const
    {
        Mat44 result
        {
            .mm
            {
                {mm[0][0],mm[1][0],mm[2][0],mm[3][0]},
                {mm[0][1],mm[1][1],mm[2][1],mm[3][1]},
                {mm[0][2],mm[1][2],mm[2][2],mm[3][2]},
                {mm[0][3],mm[1][3],mm[2][3],mm[3][3]}
            }
        };

        return result;
    }

    static const Mat44& Identity()
    {
        static constexpr const Mat44 result
        {
            .mm
            {
                {1, 0, 0, 0},
                {0, 1, 0, 0},
                {0, 0, 1, 0},
                {0, 0, 0, 1},
            }
        };

        return result;
    }

    static Mat44 PerspectiveRH(const Degrees<T> fov, const T aspect, const T nearClip, const T farClip)
    {
        return PerspectiveRH(fov.ToRadians(), aspect, nearClip, farClip);
    }

    static Mat44 PerspectiveRH(const Radians<T> fov, const T aspect, const T nearClip, const T farClip)
    {
        const T tanHalfFov = std::tan(fov.Value() / 2.0f);
        const T zRange = nearClip - farClip;

        return
        {
            .mm
            {
                {1 / (tanHalfFov * aspect), 0, 0, 0},
                {0, 1 / tanHalfFov, 0, 0},
                {0, 0, farClip / zRange , (nearClip * farClip) / zRange },
                {0, 0, -1, 0},
            }
        };
    }

    static Mat44 PerspectiveLH(const Degrees<T> fov, const T aspect, const T nearClip, const T farClip)
    {
        return PerspectiveLH(fov.ToRadians(), aspect, nearClip, farClip);
    }

    static Mat44 PerspectiveLH(const Radians<T> fov, const T aspect, const T nearClip, const T farClip)
    {
        const T tanHalfFov = std::tan(fov.Value() / 2.0f);
        const T zRange = farClip - nearClip;

        return 
        {
            .mm
            {
                {1 / (tanHalfFov * aspect), 0, 0, 0},
                {0, 1 / tanHalfFov, 0, 0},
                {0, 0, farClip / zRange , -(nearClip * farClip) / zRange },
                {0, 0, 1, 0},
            }
        };
    }
};

using Degreesf = Degrees<float>;
using Radiansf = Radians<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Quaternionf = Quaternion<float>;
using Mat44f = Mat44<float>;