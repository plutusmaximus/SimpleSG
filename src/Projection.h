#pragma once

#include "VecMath.h"

class Viewport
{
public:
    Viewport() = default;
    ~Viewport() = default;
    Viewport(const Viewport&) = default;
    Viewport& operator=(const Viewport&) = default;
    Viewport(Viewport&&) = default;
    Viewport& operator=(Viewport&&) = default;

    Viewport(const uint32_t x,
        const uint32_t y,
        const uint32_t width,
        const uint32_t height,
        const float minDepth,
        const float maxDepth);

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    uint32_t GetX() const { return m_X; }
    uint32_t GetY() const { return m_Y; }
    float GetMinDepth() const { return m_MinDepth; }
    float GetMaxDepth() const { return m_MaxDepth; }

    float GetAspectRatio() const
    {
        return static_cast<float>(m_Width) / static_cast<float>(m_Height);
    }

    bool IsValid() const
    {
        return m_Width > 0 && m_Height > 0 && m_MinDepth >= 0.0f && m_MaxDepth <= 1.0f &&
               m_MaxDepth > m_MinDepth;
    }

private:
    uint32_t m_X{0};
    uint32_t m_Y{0};
    uint32_t m_Width{0};
    uint32_t m_Height{0};
    float m_MinDepth{0.0f};
    float m_MaxDepth{1.0f};
};

class Projection
{
public:
    Projection() = default;

    void SetPerspective(const Radiansf fov,
        const float aspectRatio,
        const float nearClip,
        const float farClip,
        const Viewport& viewport);

    void SetFov(const Radiansf fov);

    Radiansf GetFov() const { return m_Fov; }

    void SetNearClip(const float nearClip);

    float GetNearClip() const { return m_Near; }

    void SetFarClip(const float farClip);

    float GetFarClip() const { return m_Far; }

    void SetAspectRatio(const float aspectRatio);

    float GetAspectRatio() const { return m_AspectRatio; }

    void SetViewport(const Viewport& viewport);

    const Viewport& GetViewport() const { return m_Viewport; }

    const Mat44f& GetMatrix() const;

private:

    constexpr static float kDefaultFovDegrees = 45.0f;
    constexpr static float kDefaultAspectRatio = 16.0f / 9.0f;
    constexpr static float kDefaultNearClip = 0.1f;
    constexpr static float kDefaultFarClip = 1000.0f;

    Radiansf m_Fov{Radiansf::FromDegrees(kDefaultFovDegrees)};
    float m_AspectRatio{kDefaultAspectRatio};
    float m_Near{ kDefaultNearClip };
    float m_Far{ kDefaultFarClip };
    Mat44f m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
    Viewport m_Viewport;
};