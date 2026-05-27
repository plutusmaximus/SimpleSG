#pragma once

#include "VecMath.h"

#include <array>

/// @brief Abstract base class for mouse navigation handling.
class MouseNav
{
public:

    virtual ~MouseNav() = 0;

    virtual void OnMouseDown(const Vec2f& mouseLoc, const Extent& screenBounds, const int mouseButton) = 0;

    virtual void OnMouseUp(const int mouseButton) = 0;

    virtual void OnKeyDown(const int keyCode) = 0;

    virtual void OnKeyUp(const int keyCode) = 0;

    virtual void OnScroll(const Vec2f& scroll) = 0;

    virtual void OnMouseMove(const Vec2f& mouseDelta) = 0;

    virtual void ClearButtons() = 0;

    virtual const TrsTransformf& GetTransform() const = 0;

    virtual void Update(const float deltaSeconds) = 0;
};

inline MouseNav::~MouseNav() = default;

/// @brief Mouse navigation implementation using walk-style controls.
///       Similar to first-person shooter controls.
///       W/A/S/D to move, mouse to look around, mouse wheel to pan up/down.
class WalkMouseNav : public MouseNav
{
public:
    /// @brief Construct a WalkMouseNav object.
    /// @param initialTransform The initial transform of the camera.
    /// @param rotPerDXY Rotation amount (in fractions of a full rotation) per unit of mouse movement in X and Y directions.
    /// @param movePerSec Movement speed in units per second.
    WalkMouseNav(
        const TrsTransformf& initialTransform,
        const float rotPerDXY,
        const float movePerSec);

    WalkMouseNav() : WalkMouseNav(TrsTransformf{}, 0.0001f, 5.0f) {}

    ~WalkMouseNav() override;

    void OnMouseDown(const Vec2f& mouseLoc, const Extent& screenBounds, const int mouseButton) override;

    void OnMouseUp(const int mouseButton) override;

    void OnKeyDown(const int keyCode) override;

    void OnKeyUp(const int keyCode) override;

    void OnScroll(const Vec2f& scroll) override;

    void OnMouseMove(const Vec2f& mouseDelta) override;

    void ClearButtons() override;

    void Update(const float deltaSeconds) override;

    void SetTransform(const TrsTransformf& transform)
    {
        m_CurrentTransform = transform;
        m_TargetTransform = transform;
    }

    const TrsTransformf& GetTransform() const override
    {
        return m_CurrentTransform;
    }

private:

    bool m_AKey{ false }, m_SKey{ false }, m_DKey{ false }, m_WKey{ false };
    Vec2f m_MouseDelta{ 0, 0};
    TrsTransformf m_CurrentTransform;
    TrsTransformf m_TargetTransform;
    const float m_MovePerSec;
    const float m_MouseMoveRotScale;
};