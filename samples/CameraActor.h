#pragma once

#include "Camera.h"

class InputMapper;

class CameraActor
{
public:
    static constexpr float kDefaultRotPerMouseMove = 0.0001f;
    static constexpr float kDefaultMovePerSec = 5.0f;

    CameraActor() = default;

    void Update(const InputMapper& inputMapper, const float deltaSeconds);

    const Camera& GetCamera() const { return m_Camera; }

    const TrTransformf& GetTransform() const { return m_CurrentTransform; }

    void SetTransform(const TrTransformf& transform)
    {
        m_CurrentTransform = transform;
        m_TargetTransform = transform;
    }

    void SetViewport(const Viewport& viewport) { m_Camera.SetViewport(viewport); }

private:
    static constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

    static UnitQuatf ClampRot(const float delta, const Vec3f& axis)
    {
        float rot = std::max(delta, -kHalfPi);
        rot = std::min(rot, kHalfPi);
        return UnitQuatf(Radiansf(rot), axis);
    }

    Camera m_Camera{ Viewport{ { .width = 1, .height = 1 } } };
    TrTransformf m_CurrentTransform = TrTransformf::Identity;
    TrTransformf m_TargetTransform = TrTransformf::Identity;
};