#pragma once

#include <vector>

#include "SceneNode.h"

class SceneVisitor;

class GroupNode : public SceneNode
{
public:

    GroupNode() = default;

    ~GroupNode() override {}

    virtual void Accept(SceneVisitor* visitor)
    {
        visitor->Visit(this);
    }

    void Traverse(SceneVisitor* visitor)
    {
        for (auto child : *this)
        {
            child->Accept(visitor);
        }
    }

    void AddChild(RefPtr<SceneNode> child)
    {
        m_Children.push_back(child);
    }

    void RemoveChild(RefPtr<SceneNode> child)
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

    using iterator = std::vector<RefPtr<SceneNode>>::iterator;
    using const_iterator = std::vector<RefPtr<SceneNode>>::const_iterator;

    iterator begin() { return m_Children.begin(); }
    const_iterator begin()const { return m_Children.begin(); }
    iterator end() { return m_Children.end(); }
    const_iterator end()const { return m_Children.end(); }

private:

    std::vector<RefPtr<SceneNode>> m_Children;
};