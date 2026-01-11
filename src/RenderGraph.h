#pragma once

#include "RefCount.h"
#include "VecMath.h"
#include "Error.h"

class Model;

class RenderGraphImpl
{
public:

    virtual ~RenderGraphImpl() = 0
    {
    }

    virtual void Add(const Mat44f& viewTransform, const Model& model) = 0;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) = 0;

protected:

    RenderGraphImpl() = default;

    IMPLEMENT_REFCOUNT(RenderGraphImpl)
};

class RenderGraph
{
public:

    RenderGraph() = default;

    explicit RenderGraph(RefPtr<RenderGraphImpl> impl)
        : m_Impl(impl)
    {
    }

    bool IsValid() const { return m_Impl != nullptr; }

    void Add(const Mat44f& viewTransform, const Model& model)
    {
        return eassert(IsValid()), m_Impl->Add(viewTransform, model);
    }

    Result<void> Render(const Mat44f& camera, const Mat44f& projection)
    {
        return eassert(IsValid()), m_Impl->Render(camera, projection);
    }

private:

    RefPtr<RenderGraphImpl> m_Impl;
};