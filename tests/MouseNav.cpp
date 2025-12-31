#include "MouseNav.h"

#include "Error.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>

//---- GimbleMouseNav Implementation ----//

GimbleMouseNav::GimbleMouseNav(const TrsTransformf& initialTransform)
    : m_StartRot(initialTransform.R)
    , m_StartTrans(initialTransform.T)
    , m_Transform(initialTransform)
{
}

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

void GimbleMouseNav::Update(const float deltaSeconds)
{
    //No per-frame update needed for gimble nav
}

void GimbleMouseNav::BeginPan(const Vec2f& mouseLoc, const float scale)
{
    eassert(&GimbleMouseNav::UpdateNothing == m_UpdateFunc);

    m_StartLoc = m_CurLoc = mouseLoc;
    m_Scale = scale;
    m_StartTrans = m_Transform.T;
    m_UpdateFunc = &GimbleMouseNav::UpdatePan;
}

void GimbleMouseNav::BeginDolly(const float scale)
{
    eassert(&GimbleMouseNav::UpdateNothing == m_UpdateFunc);

    m_Scale = scale;
    m_StartTrans = m_Transform.T;
    m_UpdateFunc = &GimbleMouseNav::UpdateDolly;
}

void GimbleMouseNav::BeginRotation(const Vec2f& mouseLoc, const Vec2f& screenBounds, const float scale)
{
    eassert(&GimbleMouseNav::UpdateNothing == m_UpdateFunc);

    m_StartLoc = m_CurLoc = mouseLoc;
    m_ScreenBounds = screenBounds;
    m_Scale = scale;
    m_StartRot = m_Transform.R;
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
    m_Transform.T = m_StartTrans + (d.x * m_Transform.LocalXAxis()) + (d.y * m_Transform.LocalYAxis());
}

void GimbleMouseNav::UpdateDolly(const Vec2f& mouseDelta)
{
    m_Transform.T = m_StartTrans + (mouseDelta.y * m_Scale * m_Transform.LocalZAxis());
}

void GimbleMouseNav::UpdateRotation(const Vec2f& mouseDelta)
{
    m_CurLoc += mouseDelta;
    const Vec2f d = (m_CurLoc - m_StartLoc) * m_Scale * 0.001f;

    const Quatf drot = Quatf(Radiansf(d.x), Vec3f::YAXIS()) * Quatf(Radiansf(d.y), Vec3f::XAXIS());
    m_Transform.R = m_StartRot * drot;
}

//---- WalkMouseNav Implementation ----//

WalkMouseNav::WalkMouseNav(
        const TrsTransformf& initialTransform,
        const float rotPerDXY,
        const float movePerSec)
    : m_Transform(initialTransform)
    , m_TargetRot(initialTransform.R.GetRotation(Vec3f::XAXIS()), initialTransform.R.GetRotation(Vec3f::YAXIS()))
    , m_TargetTrans(initialTransform.T)
    , m_MovePerSec(movePerSec)
    , m_MouseMoveRotScale(rotPerDXY * 2 * std::numbers::pi_v<float>)
{
}

WalkMouseNav::~WalkMouseNav() {}

void
WalkMouseNav::OnMouseDown(const Vec2f& mouseLoc, const Vec2f& screenBounds, const int mouseButton)
{
}

void
WalkMouseNav::OnMouseUp(const int mouseButton)
{
}

void
WalkMouseNav::OnKeyDown(const int keyCode)
{
    m_AKey = (SDL_SCANCODE_A == keyCode) ? true : m_AKey;
    m_SKey = (SDL_SCANCODE_S == keyCode) ? true : m_SKey;
    m_DKey = (SDL_SCANCODE_D == keyCode) ? true : m_DKey;
    m_WKey = (SDL_SCANCODE_W == keyCode) ? true : m_WKey;
}

void
WalkMouseNav::OnKeyUp(const int keyCode)
{
    m_AKey = (SDL_SCANCODE_A == keyCode) ? false : m_AKey;
    m_SKey = (SDL_SCANCODE_S == keyCode) ? false : m_SKey;
    m_DKey = (SDL_SCANCODE_D == keyCode) ? false : m_DKey;
    m_WKey = (SDL_SCANCODE_W == keyCode) ? false : m_WKey;
}

void
WalkMouseNav::OnScroll(const Vec2f& scroll)
{
    m_TargetTrans.y += scroll.y * m_MovePerSec * 0.1f;
}

void
WalkMouseNav::OnMouseMove(const Vec2f& mouseDelta)
{
    m_MouseDelta += mouseDelta;
}

void
WalkMouseNav::ClearButtons()
{
    m_AKey = m_SKey = m_DKey = m_WKey = false;
    m_MouseDelta = Vec2f{ 0,0 };
}

void
WalkMouseNav::Update(const float deltaSeconds)
{
    //Update rotation
    if (m_MouseDelta.x != 0 || m_MouseDelta.y != 0)
    {
        float rotX = m_MouseDelta.y * m_MouseMoveRotScale;
        float rotY = m_MouseDelta.x * m_MouseMoveRotScale;

        constexpr float PI = std::numbers::pi_v<float>;

        rotX = std::max(rotX, -(PI / 2 - 0.01f));
        rotX = std::min(rotX, PI / 2 - 0.01f);
        rotY = std::max(rotY, -(PI / 2 - 0.01f));
        rotY = std::min(rotY, PI / 2 - 0.01f);
        m_TargetRot += Vec2f{rotX, rotY};
        m_MouseDelta = Vec2f{ 0,0 };
    }

    //Update translation
    Vec3f moveDelta{ 0 };
    if (m_AKey)
    {
        moveDelta += -m_Transform.LocalXAxis() * m_MovePerSec * deltaSeconds;
    }
    if (m_DKey)
    {
        moveDelta += m_Transform.LocalXAxis() * m_MovePerSec * deltaSeconds;
    }
    if (m_WKey)
    {
        moveDelta += m_Transform.LocalZAxis() * m_MovePerSec * deltaSeconds;
    }
    if (m_SKey)
    {
        moveDelta += -m_Transform.LocalZAxis() * m_MovePerSec * deltaSeconds;
    }

    if (moveDelta.x != 0 || moveDelta.y != 0 || moveDelta.z != 0)
    {
        m_TargetTrans += moveDelta;
    }

    Quatf targetQuat = Quatf(Radiansf(m_TargetRot.y), Vec3f::YAXIS()) *
                       Quatf(Radiansf(m_TargetRot.x), Vec3f::XAXIS());
    m_Transform.R = targetQuat - ((targetQuat - m_Transform.R) * 0.1f);
    m_Transform.T += (m_TargetTrans - m_Transform.T) * 0.1f;
}