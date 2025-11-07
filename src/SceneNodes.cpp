#include "SceneNodes.h"
#include "SceneVisitors.h"

//
// GroupNode
//

void
GroupNode::Accept(SceneVisitor* visitor)
{
    visitor->Visit(this);
}

void
GroupNode::Traverse(SceneVisitor* visitor)
{
    for (auto child : *this)
    {
        child->Accept(visitor);
    }
}

void
GroupNode::AddChild(RefPtr<SceneNode> child)
{
    m_Children.push_back(child);
}

void
GroupNode::RemoveChild(RefPtr<SceneNode> child)
{
    size_t size = m_Children.size();

    for (int i = 0; i < size; ++i)
    {
        if (m_Children[i] == child)
        {
            const size_t newSize = size - 1;
            m_Children[i] = m_Children[newSize];
            size = newSize;
            --i;
        }
    }

    m_Children.resize(size);
}

//
// TransformNode
//

TransformNode::TransformNode()
{
    Transform = Mat44f::Identity();
}

void
TransformNode::Accept(SceneVisitor* visitor)
{
    visitor->Visit(this);
}

//
// CameraNode
//

Result<RefPtr<CameraNode>>
CameraNode::Create()
{
    CameraNode* camera = new CameraNode();

    expectv(camera, "Error allocating camera");

    return camera;
}

void
CameraNode::Accept(SceneVisitor* visitor)
{
    visitor->Visit(this);
}

void
CameraNode::SetPerspective(const Degreesf fov, const float width, const float height, const float nearClip, const float farClip)
{
    m_Fov = fov;
    m_Near = nearClip;
    m_Far = farClip;
    SetBounds(width, height);
}

void
CameraNode::SetBounds(const float width, const float height)
{
    if (width != m_Width || height != m_Height)
    {
        m_Width = width;
        m_Height = height;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_Width, m_Height, m_Near, m_Far);
    }
}

const Mat44f&
CameraNode::GetProjection() const
{
    return m_Proj;
}

//
// ModelNode
//

ModelNode::ModelNode(std::span<RefPtr<Mesh>> meshes)
{
    Meshes.reserve(meshes.size());
    Meshes.assign(meshes.begin(), meshes.end());
}

Result<RefPtr<ModelNode>>
ModelNode::Create(std::span<RefPtr<Mesh>> meshes)
{
    ModelNode* model = new ModelNode(meshes);

    expectv(model, "Error allocating model");

    return model;
}

void
ModelNode::Accept(SceneVisitor* visitor)
{
    visitor->Visit(this);
}