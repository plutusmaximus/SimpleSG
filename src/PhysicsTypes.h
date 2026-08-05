#pragma once

#include "AssertHelper.h"
#include "BoundingVolumes.h"

enum class MotionType
{
    Static,
    Kinematic,
    Dynamic
};

enum class CollisionType
{
    Block,
    Trigger
};

class Mass
{
public:
    Mass() = delete;
    constexpr explicit Mass(const float value)
        : m_Value(value),
          m_InvValue(value > 0.0f ? 1.0f / m_Value : 0.0f)
    {
        MLG_ABORTIF(value <= 0.0f, "Mass must be positive");
    }

    constexpr float Value() const { return m_Value; }
    constexpr float InvValue() const { return m_InvValue; }

    friend constexpr bool operator==(Mass a, Mass b) = default;
    friend constexpr auto operator<=>(Mass a, Mass b) = default;
    friend constexpr auto operator<=>(Mass a, float b) { return a.Value() <=> b; }
    friend constexpr auto operator<=>(float a, Mass b) { return b <=> a; }

    friend constexpr Mass operator+(Mass a, Mass b) { return Mass(a.Value() + b.Value()); }

    friend constexpr Mass operator-(Mass a, Mass b) { return Mass(a.Value() - b.Value()); }

    friend constexpr Mass operator*(Mass a, float scalar) { return Mass(a.Value() * scalar); }

    friend constexpr Mass operator*(float scalar, Mass a) { return a * scalar; }

    friend constexpr Mass operator/(Mass a, float scalar) { return Mass(a.Value() / scalar); }

    // Division of mass by mass results in a dimensionless ratio.
    friend constexpr float operator/(Mass a, Mass b) { return a.Value() / b.Value(); }

    constexpr Mass operator-() const { return Mass(-Value()); }

    constexpr Mass& operator+=(Mass other)
    {
        *this = *this + other;
        return *this;
    }

    constexpr Mass& operator-=(Mass other) { return *this = *this - other; }

    constexpr Mass& operator*=(float scalar) { return *this = *this * scalar; }

    constexpr Mass& operator/=(float scalar) { return *this = *this / scalar; }

private:
    float m_Value;
    float m_InvValue; // Inverse value
};

class Collider
{
public:
    Collider() = delete;

    Collider(const BoundingVolume& boundingVolume, const CollisionType collisionType)
        : m_BoundingVolume(boundingVolume),
          m_CollisionType(collisionType)
    {
    }

    const BoundingVolume& GetBoundingVolume() const { return m_BoundingVolume; }

    const BoundingSphere& GetBoundingSphere() const
    {
        return m_BoundingVolume.GetEnclosingSphere();
    }

    CollisionType GetCollisionType() const { return m_CollisionType; }

private:
    BoundingVolume m_BoundingVolume;
    CollisionType m_CollisionType;
};

class RigidBody
{
public:
    RigidBody() = delete;

    explicit RigidBody(
        const Mass mass, const MotionType motionType, const std::span<const Collider> colliders)
        : m_Mass(mass),
          m_MotionType(motionType),
          m_Colliders(colliders)
    {
        MLG_ABORTIF(colliders.empty(), "RigidBody must have at least one collider");
    }

    Mass GetMass() const { return m_Mass; }

    std::span<const Collider> GetColliders() const { return m_Colliders; }

    MotionType GetMotionType() const { return m_MotionType; }

private:
    Mass m_Mass;
    MotionType m_MotionType;
    std::span<const Collider> m_Colliders;
};