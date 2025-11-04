#include "MouseNav.h"

#include "Error.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>

GimbleMouseNav::~GimbleMouseNav() {}

void GimbleMouseNav::OnMouseDown(const Vec2f& mouseLoc, const Vec2f& screenBounds, const int mouseButton)
{
    if (1 == mouseButton)
    {
        m_MouseButtons[1] = true;

        if (m_LeftShift || m_RightShift)
        {
            BeginPan(mouseLoc, 0.01f);
            m_Panning = true;
        }
        else
        {
            BeginRotation(mouseLoc, screenBounds, 1);
        }
    }
}

void GimbleMouseNav::OnMouseUp(const int mouseButton)
{
    if (1 == mouseButton)
    {
        m_MouseButtons[1] = false;

        if (m_Panning)
        {
            EndPan();
            m_Panning = false;
        }
        else
        {
            EndRotation();
        }
    }
}

void GimbleMouseNav::OnKeyDown(const int keyCode)
{
    if (SDL_SCANCODE_LSHIFT == keyCode)
    {
        m_LeftShift = true;
    }
    else if (SDL_SCANCODE_RSHIFT == keyCode)
    {
        m_RightShift = true;
    }
}

void GimbleMouseNav::OnKeyUp(const int keyCode)
{
    if (SDL_SCANCODE_LSHIFT == keyCode)
    {
        m_LeftShift = false;
    }
    else if (SDL_SCANCODE_RSHIFT == keyCode)
    {
        m_RightShift = false;
    }
}

void GimbleMouseNav::OnScroll(const Vec2f& scroll)
{
    //Make sure no mouse buttons are pressed
    if (!std::ranges::contains(m_MouseButtons, true))
    {
        BeginDolly(1);
        UpdateDolly(scroll);
        EndDolly();
    }
}

void GimbleMouseNav::OnMouseMove(const Vec2f& mouseDelta)
{
    (this->*m_UpdateFunc)(mouseDelta);
}

void GimbleMouseNav::ClearButtons()
{
    m_MouseButtons.fill(false);
}

void GimbleMouseNav::BeginPan(const Vec2f& mouseLoc, const float scale)
{
    eassert(&GimbleMouseNav::UpdateNothing == m_UpdateFunc);

    m_StartLoc = m_CurLoc = mouseLoc;
    m_Transform = m_TransformNode->Transform;
    m_Scale = scale;
    m_UpdateFunc = &GimbleMouseNav::UpdatePan;
}

void GimbleMouseNav::BeginDolly(const float scale)
{
    eassert(&GimbleMouseNav::UpdateNothing == m_UpdateFunc);

    m_Transform = m_TransformNode->Transform;
    m_Scale = scale;
    m_UpdateFunc = &GimbleMouseNav::UpdateDolly;
}

void GimbleMouseNav::BeginRotation(const Vec2f& mouseLoc, const Vec2f& screenBounds, const float scale)
{
    eassert(&GimbleMouseNav::UpdateNothing == m_UpdateFunc);

    m_StartLoc = m_CurLoc = mouseLoc;
    m_ScreenBounds = screenBounds;
    m_Transform = m_TransformNode->Transform;
    m_Scale = scale;
    m_UpdateFunc = &GimbleMouseNav::UpdateRotation;
}

void GimbleMouseNav::EndPan()
{
    eassert(&GimbleMouseNav::UpdatePan == m_UpdateFunc);

    m_UpdateFunc = &GimbleMouseNav::UpdateNothing;
}

void GimbleMouseNav::EndDolly()
{
    eassert(&GimbleMouseNav::UpdateDolly == m_UpdateFunc);

    m_UpdateFunc = &GimbleMouseNav::UpdateNothing;
}

void GimbleMouseNav::EndRotation()
{
    eassert(&GimbleMouseNav::UpdateRotation == m_UpdateFunc);

    m_UpdateFunc = &GimbleMouseNav::UpdateNothing;
}

void GimbleMouseNav::UpdateNothing(const Vec2f&)
{
}

void GimbleMouseNav::UpdatePan(const Vec2f& mouseDelta)
{
    m_CurLoc.x += mouseDelta.x;
    m_CurLoc.y -= mouseDelta.y;
    Vec2f d = (m_CurLoc - m_StartLoc) * m_Scale;
    m_TransformNode->Transform = m_Transform.Translate(d.x, d.y, 0);
}

void GimbleMouseNav::UpdateDolly(const Vec2f& mouseDelta)
{
    m_TransformNode->Transform = m_Transform.Translate(0, 0, mouseDelta.y * m_Scale);
}

void GimbleMouseNav::UpdateRotation(const Vec2f& mouseDelta)
{
    m_CurLoc += mouseDelta;
    const Vec2f d = (m_CurLoc - m_StartLoc) * m_Scale * 0.001f;

    const Mat44f rot = Mat44f::Identity()
        .Rotate(Radiansf(d.x), Vec3f::YAXIS())
        .Rotate(Radiansf(d.y), Vec3f::XAXIS());

    m_TransformNode->Transform = m_Transform * rot;
}
