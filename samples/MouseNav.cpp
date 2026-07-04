#include "MouseNav.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <numbers>

//---- WalkMouseNav Implementation ----//

WalkMouseNav::WalkMouseNav(
        const Posef& initialTransform,
        const float /*rotPerDXY*/,
        const float movePerSec)
    : m_CurrentTransform(initialTransform)
    , m_TargetTransform(initialTransform)
    , m_MovePerSec(movePerSec)
    //, m_MouseMoveRotScale(rotPerDXY * 2 * std::numbers::pi_v<float>)
{
}

void
WalkMouseNav::OnKeyDown(const int)
{
}

void
WalkMouseNav::OnKeyUp(const int)
{
}

void
WalkMouseNav::OnScroll(const Vec2f& scroll)
{
    constexpr float kScrollMoveScale = 0.1f;

    m_TargetTransform.T.y += scroll.y * m_MovePerSec * kScrollMoveScale;
}

void
WalkMouseNav::OnMouseMove(const Vec2f& mouseDelta)
{
    m_LookDelta += mouseDelta;
}

void
WalkMouseNav::ClearButtons()
{
    m_LookDelta = Vec2f{ 0,0 };
}

void
WalkMouseNav::Update(const float deltaSeconds)
{
    const Vec2f lookDelta = m_LookDelta;
    const Vec3f moveDelta = m_MoveDelta;

    m_LookDelta = Vec2f(0);
    m_MoveDelta = Vec3f(0);

    if(!m_IsActive)
    {
        return;
    }

    //Update rotation
    if (lookDelta.x != 0 || lookDelta.y != 0)
    {
        float rotX = lookDelta.y;// * m_MouseMoveRotScale;
        float rotY = lookDelta.x;// * m_MouseMoveRotScale;

        constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

        rotX = std::max(rotX, -kHalfPi);
        rotX = std::min(rotX, kHalfPi);
        rotY = std::max(rotY, -kHalfPi);
        rotY = std::min(rotY, kHalfPi);
        const UnitQuatf pitch = UnitQuatf(Radiansf(rotX), Vec3f::XAXIS());
        const UnitQuatf yaw = UnitQuatf(Radiansf(rotY), Vec3f::YAXIS());

        m_TargetTransform.R = yaw * m_TargetTransform.R * pitch;
    }

    //Update translation
    m_TargetTransform.T += moveDelta * m_MovePerSec * deltaSeconds;

    constexpr float kTransformTimeToTarget = 0.1f;
    //constexpr float kRotationTimeToTarget = 0.01f;

    const float dtT = deltaSeconds / kTransformTimeToTarget;
    //const float dtR = deltaSeconds / kRotationTimeToTarget;

    m_CurrentTransform.T = m_CurrentTransform.T.Lerp(m_TargetTransform.T, dtT);
    //m_CurrentTransform.R = m_CurrentTransform.R.Lerp(m_TargetTransform.R, dtR);
    m_CurrentTransform.R = m_TargetTransform.R;
}

void
WalkMouseNav::Look(const Vec2f& delta)
{
    m_LookDelta += delta;
}

void
WalkMouseNav::Move(const Vec3f& delta)
{
    m_MoveDelta += m_CurrentTransform.R * delta;
}

void
WalkMouseNav::Activate()
{
    m_IsActive = true;
}

void
WalkMouseNav::Deactivate()
{
    m_IsActive = false;
    ClearButtons();
}