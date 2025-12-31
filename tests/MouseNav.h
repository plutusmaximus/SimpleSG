#pragma once

#include "VecMath.h"

#include <array>

/// @brief Abstract base class for mouse navigation handling.
class MouseNav
{
public:

    virtual ~MouseNav() = 0 {}

    virtual void OnMouseDown(const Point& mouseLoc, const Extent& screenBounds, const int mouseButton) = 0;

    virtual void OnMouseUp(const int mouseButton) = 0;

    virtual void OnKeyDown(const int keyCode) = 0;

    virtual void OnKeyUp(const int keyCode) = 0;

    virtual void OnScroll(const Vec2f& scroll) = 0;

    virtual void OnMouseMove(const Vec2f& mouseDelta) = 0;

    virtual void ClearButtons() = 0;

    virtual const TrsTransformf& GetTransform() const = 0;

    virtual void Update(const float deltaSeconds) = 0;
};

/// @brief Mouse navigation implementation using gimble-style controls.
class GimbleMouseNav : public MouseNav
{
public:
    explicit GimbleMouseNav(const TrsTransformf& initialTransform);

    ~GimbleMouseNav() override;

    void OnMouseDown(const Point& mouseLoc, const Extent& screenBounds, const int mouseButton) override;

    void OnMouseUp(const int mouseButton) override;

    void OnKeyDown(const int keyCode) override;

    void OnKeyUp(const int keyCode) override;

    void OnScroll(const Vec2f& scroll) override;

    void OnMouseMove(const Vec2f& mouseDelta) override;

    void ClearButtons() override;

    void Update(const float deltaSeconds) override;

    void SetTransform(const TrsTransformf& transform)
    {
        m_Transform = transform;
    }

    const TrsTransformf& GetTransform() const override
    {
        return m_Transform;
    }

private:

    void BeginPan(const Point& mouseLoc, const float scale);

    void BeginDolly(const float scale);

    void BeginRotation(const Point& mouseLoc, const Extent& screenBounds, const float scale);

    void EndPan();

    void EndDolly();

    void EndRotation();

    void UpdateNothing(const Vec2f&);

    void UpdatePan(const Vec2f& mouseDelta);

    void UpdateDolly(const Vec2f& mouseDelta);

    void UpdateRotation(const Vec2f& mouseDelta);

    std::array<bool, 3> m_MouseButtons{ false };
    Point m_StartLoc{ 0,0 };
    Point m_CurLoc{ 0,0 };
    Extent m_ScreenBounds{ 0,0 };
    Quatf m_StartRot;
    Vec3f m_StartTrans;
    TrsTransformf m_Transform;
    float m_Scale{ 1 };
    bool m_LeftShift{ false }, m_RightShift{ false };
    bool m_Panning{ false };

    void (GimbleMouseNav::* m_UpdateFunc)(const Vec2f& delta) { &GimbleMouseNav::UpdateNothing };
};

/// @brief Mouse navigation implementation using walk-style controls.
///       Similar to first-person shooter controls.
///       W/A/S/D to move, mouse to look around.
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

    ~WalkMouseNav() override;

    void OnMouseDown(const Point& mouseLoc, const Extent& screenBounds, const int mouseButton) override;

    void OnMouseUp(const int mouseButton) override;

    void OnKeyDown(const int keyCode) override;

    void OnKeyUp(const int keyCode) override;

    void OnScroll(const Vec2f& scroll) override;

    void OnMouseMove(const Vec2f& mouseDelta) override;

    void ClearButtons() override;

    void Update(const float deltaSeconds) override;

    void SetTransform(const TrsTransformf& transform)
    {
        m_Transform = transform;
        m_TargetRot = { transform.R.GetRotation(Vec3f::XAXIS()), transform.R.GetRotation(Vec3f::YAXIS()) };
        m_TargetTrans = transform.T;
    }

    const TrsTransformf& GetTransform() const override
    {
        return m_Transform;
    }

private:

    bool m_AKey{ false }, m_SKey{ false }, m_DKey{ false }, m_WKey{ false };
    Vec2f m_MouseDelta{ 0,0 };
    TrsTransformf m_Transform;
    Vec2f m_TargetRot;
    Vec3f m_TargetTrans;
    const float m_MovePerSec;
    const float m_MouseMoveRotScale;
};