#include "CameraActor.h"

#include "CommonActionIds.h"

void
CameraActor::Update(const InputMapper& inputMapper, const float deltaSeconds)
{
    float actionValue = 0;

    UnitQuatf yaw = UnitQuatf::Identity, pitch = UnitQuatf::Identity;
    Vec3f moveDelta(0);

    if(inputMapper.IsActionTriggered(CommonActionIds::MoveForward, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, 0, actionValue);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveBackward, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, 0, actionValue);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveLeft, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(actionValue, 0, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveRight, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(actionValue, 0, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveUpDown, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::LookLeftRight, actionValue))
    {
        yaw = ClampRot(actionValue, Vec3f::YAXIS());
        m_TargetTransform.R = yaw * m_TargetTransform.R;
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::LookUpDown, actionValue))
    {
        pitch = ClampRot(actionValue, Vec3f::XAXIS());
        m_TargetTransform.R = m_TargetTransform.R * pitch;
    }

    m_TargetTransform.T += moveDelta * kDefaultMovePerSec * deltaSeconds;

    constexpr float kTransformTimeToTarget = 0.1f;
    // constexpr float kRotationTimeToTarget = 0.01f;

    const float dtT = deltaSeconds / kTransformTimeToTarget;
    // const float dtR = deltaSeconds / kRotationTimeToTarget;

    m_CurrentTransform.T = m_CurrentTransform.T.Lerp(m_TargetTransform.T, dtT);
    // m_CurrentTransform.R = m_CurrentTransform.R.Lerp(m_TargetTransform.R, dtR);
    m_CurrentTransform.R = m_TargetTransform.R;
}
