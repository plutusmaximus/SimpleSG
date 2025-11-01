#pragma once

#include "VecMath.h"
#include "SceneNode.h"
#include "SceneVisitor.h"

class Camera : public SceneNode
{
public:

    static Result<RefPtr<Camera>> Create()
    {
        Camera* camera = new Camera();

        expectv(camera, "Error allocating camera");

        return camera;
    }

    ~Camera() override {}

    void Accept(SceneVisitor* visitor) override
    {
        visitor->Visit(this);
    }

    void SetPerspective(const Degreesf fov, const float width, const float height, const float nearClip, const float farClip)
    {
        m_Fov = fov;
        m_Near = nearClip;
        m_Far = farClip;
        SetBounds(width, height);
    }

    void SetBounds(const float width, const float height)
    {
        if (width != m_Width || height != m_Height)
        {
            m_Width = width;
            m_Height = height;
            m_Proj = Mat44f::PerspectiveLH(m_Fov, m_Width, m_Height, m_Near, m_Far);
        }
    }

    const Mat44f& GetProjection() const
    {
        return m_Proj;
    }

private:

    Camera() = default;

    Degreesf m_Fov{ 0 };
    float m_Width{ 0 }, m_Height{ 0 };
    float m_Near{ 0 };
    float m_Far{ 0 };
    Mat44f m_Proj{ 1 };
};