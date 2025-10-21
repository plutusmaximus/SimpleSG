#pragma once

#include "Model.h"
#include "RenderGraph.h"

class GPUDevice
{
public:

    GPUDevice() {}

    virtual ~GPUDevice() = 0 {}

    virtual std::expected<RefPtr<Model>, Error> CreateModel(const ModelSpec& modelSpec) = 0;

    virtual std::expected<RefPtr<RenderGraph>, Error> CreateRenderGraph() = 0;

    IMPLEMENT_REFCOUNT(GPUDevice);
};