#pragma once

#include "AssertHelper.h"
#include "VecMath.h"

class Camera;
class BoundingSphere;

class Viewport
{
public:
    Viewport() = delete;

    struct ViewportParams
    {
        uint32_t x = 0, y = 0, width, height;
        float minDepth = 0, maxDepth = 1;
    };

    explicit Viewport(const ViewportParams& params);

    explicit Viewport(const Dimension2& dimensions)
        : Viewport({ .x = 0,
              .y = 0,
              .width = dimensions.Width,
              .height = dimensions.Height,
              .minDepth = 0,
              .maxDepth = 1 })
    {
        MLG_ABORTIF(dimensions.Width == 0, "Width must be non-zero");
        MLG_ABORTIF(dimensions.Height == 0, "Height must be non-zero");
    }

    explicit Viewport(const Rect& bounds)
        : Viewport({ .x = static_cast<uint32_t>(bounds.GetX()),
              .y = static_cast<uint32_t>(bounds.GetY()),
              .width = bounds.GetWidth(),
              .height = bounds.GetHeight(),
              .minDepth = 0,
              .maxDepth = 1 })
    {
        MLG_ABORTIF(bounds.GetWidth() == 0, "Width must be non-zero");
        MLG_ABORTIF(bounds.GetHeight() == 0, "Height must be non-zero");
        MLG_ABORTIF(bounds.GetX() < 0, "X must be non-negative");
        MLG_ABORTIF(bounds.GetY() < 0, "Y must be non-negative");
    }

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

private:
    uint32_t m_X{0};
    uint32_t m_Y{0};
    uint32_t m_Width{0};
    uint32_t m_Height{0};
    float m_MinDepth{0.0f};
    float m_MaxDepth{1.0f};
};

class Frustum
{
public:

    Frustum() = delete;
    Frustum(const Camera& camera, const TrTransformf& cameraXForm);
    Frustum(const Camera& camera, const TrTransformf& cameraXForm, const Rect& selectRect);

    enum class ContainsResult
    {
        Outside,
        Intersects,
        Inside,
    };

    ContainsResult Contains(const BoundingSphere& sphere) const;

    const Vec4f& GetLeft() const { return m_Planes[kLeft]; }
    const Vec4f& GetRight() const { return m_Planes[kRight]; }
    const Vec4f& GetTop() const { return m_Planes[kTop]; }
    const Vec4f& GetBottom() const { return m_Planes[kBottom]; }
    const Vec4f& GetNear() const { return m_Planes[kNear]; }
    const Vec4f& GetFar() const { return m_Planes[kFar]; }

private:
    friend Camera;

    Frustum(const Vec4f& left,
        const Vec4f& right,
        const Vec4f& top,
        const Vec4f& bottom,
        const Vec4f& near,
        const Vec4f& far)
        : m_Planes {left, right, top, bottom, near, far}
    {
    }

    constexpr static size_t kLeft = 0;
    constexpr static size_t kRight = 1;
    constexpr static size_t kTop = 2;
    constexpr static size_t kBottom = 3;
    constexpr static size_t kNear = 4;
    constexpr static size_t kFar = 5;
    constexpr static size_t kNumPlanes = 6;

    Vec4f m_Planes[kNumPlanes];
};

class Camera
{
public:
    Camera() = delete;
    
    explicit Camera(const Viewport& viewport)
        : m_Viewport(viewport)
    {
        SetAspectRatio(viewport.GetAspectRatio());
    }

    void SetPerspective(const Radiansf fov,
        const float aspectRatio,
        const float nearClip,
        const float farClip,
        const Viewport& viewport);

    void SetFov(const Radiansf fov);

    Radiansf GetFov() const { return m_Fov; }

    void SetNearClip(const float nearClip);

    float GetNearClip() const { return m_NearClip; }

    void SetFarClip(const float farClip);

    float GetFarClip() const { return m_FarClip; }

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
    float m_NearClip{ kDefaultNearClip };
    float m_FarClip{ kDefaultFarClip };
    Mat44f m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
    Viewport m_Viewport;
};