#pragma once

#include "ModelNode.h"
#include "RenderGraph.h"

class GPUDevice
{
public:

    GPUDevice() {}

    virtual ~GPUDevice() = 0 {}

    virtual Result<RefPtr<ModelNode>> CreateModel(const ModelSpec& modelSpec) = 0;

    virtual Result<RefPtr<RenderGraph>> CreateRenderGraph() = 0;

    IMPLEMENT_REFCOUNT(GPUDevice);
};