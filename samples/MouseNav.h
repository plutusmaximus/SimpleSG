#pragma once

#include "VecMath.h"

#include <array>

/// @brief Abstract base class for mouse navigation handling.
class MouseNav
{
public:

    MouseNav() = default;
    virtual ~MouseNav() = 0;
    MouseNav(const MouseNav&) = delete;
    MouseNav& operator=(const MouseNav&) = delete;
    MouseNav(MouseNav&&) = delete;
    MouseNav& operator=(MouseNav&&) = delete;

    virtual void OnMouseDown(const Vec2f& mouseLoc, const Extent& screenBounds, const int mouseButton) = 0;

    virtual void OnMouseUp(const int mouseButton) = 0;

    virtual void OnKeyDown(const int keyCode) = 0;

    virtual void OnKeyUp(const int keyCode) = 0;

    virtual void OnScroll(const Vec2f& scroll) = 0;

    virtual void OnMouseMove(const Vec2f& mouseDelta) = 0;

    virtual void ClearButtons() = 0;

    virtual const Posef& GetTransform() const = 0;

    virtual void Update(const float deltaSeconds) = 0;
};

inline MouseNav::~MouseNav() = default;

/// @brief Mouse navigation implementation using walk-style controls.
///       Similar to first-person shooter controls.
///       W/A/S/D to move, mouse to look around, mouse wheel to pan up/down.
class WalkMouseNav : public MouseNav
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

    ~WalkMouseNav() override;
    WalkMouseNav(const WalkMouseNav&) = delete;
    WalkMouseNav& operator=(const WalkMouseNav&) = delete;
    WalkMouseNav(WalkMouseNav&&) = delete;
    WalkMouseNav& operator=(WalkMouseNav&&) = delete;

    void OnMouseDown(const Vec2f& mouseLoc, const Extent& screenBounds, const int mouseButton) override;

    void OnMouseUp(const int mouseButton) override;

    void OnKeyDown(const int keyCode) override;

    void OnKeyUp(const int keyCode) override;

    void OnScroll(const Vec2f& scroll) override;

    void OnMouseMove(const Vec2f& mouseDelta) override;

    void ClearButtons() override;

    void Update(const float deltaSeconds) override;

    void SetTransform(const Posef& transform)
    {
        m_CurrentTransform = transform;
        m_TargetTransform = transform;
    }

    const Posef& GetTransform() const override
    {
        return m_CurrentTransform;
    }

private:

    bool m_AKey{ false }, m_SKey{ false }, m_DKey{ false }, m_WKey{ false };
    Vec2f m_MouseDelta{ 0, 0};
    Posef m_CurrentTransform;
    Posef m_TargetTransform;
    const float m_MovePerSec;
    const float m_MouseMoveRotScale;
};