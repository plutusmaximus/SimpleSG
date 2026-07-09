#pragma once

#include "VecMath.h"

/// @brief Mouse navigation implementation using walk-style controls.
///       Similar to first-person shooter controls.
///       W/A/S/D to move, mouse to look around, mouse wheel to pan up/down.
class WalkMouseNav
{
public:
    static constexpr float kDefualtRotPerDXY = 0.0001f;
    static constexpr float kDefaultMovePerSec = 5.0f;

    /// @brief Construct a WalkMouseNav object.
    /// @param initialTransform The initial transform of the camera.
    /// @param rotPerDXY Rotation amount (in fractions of a full rotation) per unit of mouse movement in X and Y directions.
    /// @param movePerSec Movement speed in units per second.
    WalkMouseNav(
        const TrTransformf& initialTransform,
        const float rotPerDXY,
        const float movePerSec);

    WalkMouseNav() : WalkMouseNav(TrTransformf{}, kDefualtRotPerDXY, kDefaultMovePerSec) {}

    ~WalkMouseNav() = default;
    WalkMouseNav(const WalkMouseNav&) = delete;
    WalkMouseNav& operator=(const WalkMouseNav&) = delete;
    WalkMouseNav(WalkMouseNav&&) = delete;
    WalkMouseNav& operator=(WalkMouseNav&&) = delete;

    void Update(const float deltaSeconds);

    void Look(const Vec2f& delta);

    void Move(const Vec3f& delta);

    void SetTransform(const TrTransformf& transform)
    {
        m_CurrentTransform = transform;
        m_TargetTransform = transform;
    }

    void Activate();

    void Deactivate();

    const TrTransformf& GetTransform() const
    {
        return m_CurrentTransform;
    }

private:

    Vec2f m_LookDelta{ 0, 0};
    Vec3f m_MoveDelta{ 0, 0, 0 };
    TrTransformf m_CurrentTransform;
    TrTransformf m_TargetTransform;
    const float m_MovePerSec;
    //const float m_MouseMoveRotScale;
    bool m_IsActive{ false };
};