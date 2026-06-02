#include "MouseNav.h"

#include "AssertHelper.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <numbers>

//---- WalkMouseNav Implementation ----//

WalkMouseNav::WalkMouseNav(
        const TrsTransformf& initialTransform,
        const float rotPerDXY,
        const float movePerSec)
    : m_CurrentTransform(initialTransform)
    , m_TargetTransform(initialTransform)
    , m_MovePerSec(movePerSec)
    , m_MouseMoveRotScale(rotPerDXY * 2 * std::numbers::pi_v<float>)
{
}

WalkMouseNav::~WalkMouseNav() = default;

void
WalkMouseNav::OnMouseDown(const Vec2f& /*mouseLoc*/, const Extent& /*screenBounds*/, const int /*mouseButton*/)
{
}

void
WalkMouseNav::OnMouseUp(const int /*mouseButton*/)
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
    constexpr float kScrollMoveScale = 0.1f;

    m_TargetTransform.T.y += scroll.y * m_MovePerSec * kScrollMoveScale;
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

        constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

        rotX = std::max(rotX, -kHalfPi);
        rotX = std::min(rotX, kHalfPi);
        rotY = std::max(rotY, -kHalfPi);
        rotY = std::min(rotY, kHalfPi);
        const UnitQuatf pitch = UnitQuatf(Radiansf(rotX), Vec3f::XAXIS());
        const UnitQuatf yaw = UnitQuatf(Radiansf(rotY), Vec3f::YAXIS());

        m_TargetTransform.R = yaw * m_TargetTransform.R * pitch;
        m_MouseDelta = Vec2f{ 0,0 };
    }

    //Update translation
    Vec3f moveDelta{ 0 };
    if (m_AKey)
    {
        moveDelta += -m_CurrentTransform.LocalXAxis() * m_MovePerSec * deltaSeconds;
    }
    if (m_DKey)
    {
        moveDelta += m_CurrentTransform.LocalXAxis() * m_MovePerSec * deltaSeconds;
    }
    if (m_WKey)
    {
        moveDelta += m_CurrentTransform.LocalZAxis() * m_MovePerSec * deltaSeconds;
    }
    if (m_SKey)
    {
        moveDelta += -m_CurrentTransform.LocalZAxis() * m_MovePerSec * deltaSeconds;
    }

    if (moveDelta.x != 0 || moveDelta.y != 0 || moveDelta.z != 0)
    {
        m_TargetTransform.T += moveDelta;
    }

    constexpr float kTransformTimeToTarget = 0.1f;
    //constexpr float kRotationTimeToTarget = 0.01f;

    const float dtT = deltaSeconds / kTransformTimeToTarget;
    //const float dtR = deltaSeconds / kRotationTimeToTarget;

    m_CurrentTransform.T = m_CurrentTransform.T.Lerp(m_TargetTransform.T, dtT);
    //m_CurrentTransform.R = m_CurrentTransform.R.Lerp(m_TargetTransform.R, dtR);
    m_CurrentTransform.R = m_TargetTransform.R;
}