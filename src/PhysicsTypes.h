#pragma once

#include "AssertHelper.h"
#include "Bounds.h"
#include "VecMath.h"

#include <variant>

class Mass
{
public:
    Mass() = delete;
    constexpr explicit Mass(const float value)
        : m_Value(value),
          m_InvValue(value > 0.0f ? 1.0f / m_Value : 0.0f)
    {
        MLG_REQUIRE(value > 0.0f, "Mass must be positive");
    }

    constexpr float Value() const { return m_Value; }
    constexpr float InvValue() const { return m_InvValue; }

    friend constexpr bool operator==(Mass a, Mass b) = default;
    friend constexpr auto operator<=>(Mass a, Mass b) = default;
    friend constexpr auto operator<=>(Mass a, float b)
    {
        return a.Value() <=> b;
    }
    friend constexpr auto operator<=>(float a, Mass b)
    {
        return b <=> a;
    }

    friend constexpr Mass operator+(Mass a, Mass b)
    {
        return Mass(a.Value() + b.Value());
    }

    friend constexpr Mass operator-(Mass a, Mass b)
    {
        return Mass(a.Value() - b.Value());
    }

    friend constexpr Mass operator*(Mass a, float scalar)
    {
        return Mass(a.Value() * scalar);
    }

    friend constexpr Mass operator*(float scalar, Mass a)
    {
        return a * scalar;
    }

    friend constexpr Mass operator/(Mass a, float scalar)
    {
        return Mass(a.Value() / scalar);
    }

    // Division of mass by mass results in a dimensionless ratio.
    friend constexpr float operator/(Mass a, Mass b)
    {
        return a.Value() / b.Value();
    }

    constexpr Mass operator-() const
    {
        return Mass(-Value());
    }

    constexpr Mass& operator+=(Mass other)
    {
        *this = *this + other;
        return *this;
    }

    constexpr Mass& operator-=(Mass other)
    {
        return *this = *this - other;
    }

    constexpr Mass& operator*=(float scalar)
    {
        return *this = *this * scalar;
    }

    constexpr Mass& operator/=(float scalar)
    {
        return *this = *this / scalar;
    }

private:
    float m_Value;
    float m_InvValue;   // Inverse value
};

class RigidBody
{
public:

    RigidBody() = delete;

    explicit RigidBody(const Mass mass)
        : m_Mass(mass)
    {
    }
    Mass GetMass() const { return m_Mass; }
    float GetInvMass() const { return m_Mass.InvValue(); }

private:

    Mass m_Mass;
};

class Collider
{
public:

    Collider() = delete;

    explicit Collider(const Sphere& sphere)
        : m_Shape(sphere)
        , m_SphereRadius(sphere.GetRadius())
    {
    }

    explicit Collider(const Box& box)
        : m_Shape(box)
        , m_SphereRadius(box.GetHalfExtents().Length())
    {
    }

    explicit Collider(const Capsule& capsule)
        : m_Shape(capsule)
        , m_SphereRadius(capsule.GetRadius() + capsule.GetHalfHeight())
    {
    }

    const std::variant<Sphere, Box, Capsule>& GetShape() const { return m_Shape; }

    float GetSphereRadius() const
    {
        return m_SphereRadius;
    }

private:

    std::variant<Sphere, Box, Capsule> m_Shape;
    float m_SphereRadius{ 0 };
};