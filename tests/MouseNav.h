#pragma once

#include "VecMath.h"
#include "SceneNodes.h"

#include <array>

class MouseNav
{
public:

    virtual ~MouseNav() = 0 {}

    virtual void OnMouseDown(const Vec2f& mouseLoc, const Vec2f& screenBounds, const int mouseButton) = 0;

    virtual void OnMouseUp(const int mouseButton) = 0;

    virtual void OnKeyDown(const int keyCode) = 0;

    virtual void OnKeyUp(const int keyCode) = 0;

    virtual void OnScroll(const Vec2f& scroll) = 0;

    virtual void OnMouseMove(const Vec2f& mouseDelta) = 0;

    virtual void ClearButtons() = 0;
};

class GimbleMouseNav : public MouseNav
{
public:
    explicit GimbleMouseNav(RefPtr<TransformNode> transformNode);

    ~GimbleMouseNav() override;

    void OnMouseDown(const Vec2f& mouseLoc, const Vec2f& screenBounds, const int mouseButton) override;

    void OnMouseUp(const int mouseButton) override;

    void OnKeyDown(const int keyCode) override;

    void OnKeyUp(const int keyCode) override;

    void OnScroll(const Vec2f& scroll) override;

    void OnMouseMove(const Vec2f& mouseDelta) override;

    void ClearButtons();

private:

    void BeginPan(const Vec2f& mouseLoc, const float scale);

    void BeginDolly(const float scale);

    void BeginRotation(const Vec2f& mouseLoc, const Vec2f& screenBounds, const float scale);

    void EndPan();

    void EndDolly();

    void EndRotation();

    void UpdateNothing(const Vec2f&);

    void UpdatePan(const Vec2f& mouseDelta);

    void UpdateDolly(const Vec2f& mouseDelta);

    void UpdateRotation(const Vec2f& mouseDelta);

    RefPtr<TransformNode> m_TransformNode;
    std::array<bool, 3> m_MouseButtons{ false };
    Vec2f m_StartLoc{ 0,0 };
    Vec2f m_CurLoc{ 0,0 };
    Vec2f m_ScreenBounds{ 0,0 };
    Mat44f m_Transform{ 1 };
    float m_Scale{ 1 };
    bool m_LeftShift{ false }, m_RightShift{ false };
    bool m_Panning{ false };

    void (GimbleMouseNav::* m_UpdateFunc)(const Vec2f& delta) { &GimbleMouseNav::UpdateNothing };
};