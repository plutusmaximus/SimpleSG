#pragma once

// This file contains classes and functions related to physics, dynamics, kinematics, and statics.

#include "AssertHelper.h"

class Mass
{
public:
    Mass() = delete;
    constexpr explicit Mass(float value)
        : m_Value(value)
    {
        MLG_ASSERT(value > 0.0f, "Mass must be positive");
        m_InvValue = 1.0f / value;
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