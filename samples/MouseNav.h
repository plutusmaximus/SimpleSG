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
        const Posef& initialTransform,
        const float rotPerDXY,
        const float movePerSec);

    WalkMouseNav() : WalkMouseNav(Posef{}, kDefualtRotPerDXY, kDefaultMovePerSec) {}

    ~WalkMouseNav() = default;
    WalkMouseNav(const WalkMouseNav&) = delete;
    WalkMouseNav& operator=(const WalkMouseNav&) = delete;
    WalkMouseNav(WalkMouseNav&&) = delete;
    WalkMouseNav& operator=(WalkMouseNav&&) = delete;

    void OnKeyDown(const int keyCode);

    void OnKeyUp(const int keyCode);

    void OnScroll(const Vec2f& scroll);

    void OnMouseMove(const Vec2f& mouseDelta);

    void ClearButtons();

    void Update(const float deltaSeconds);

    void Look(const Vec2f& delta);

    void Move(const Vec3f& delta);

    void SetTransform(const Posef& transform)
    {
        m_CurrentTransform = transform;
        m_TargetTransform = transform;
    }

    void Activate();

    void Deactivate();

    const Posef& GetTransform() const
    {
        return m_CurrentTransform;
    }

private:

    Vec2f m_LookDelta{ 0, 0};
    Vec3f m_MoveDelta{ 0, 0, 0 };
    Posef m_CurrentTransform;
    Posef m_TargetTransform;
    const float m_MovePerSec;
    //const float m_MouseMoveRotScale;
    bool m_IsActive{ false };
};