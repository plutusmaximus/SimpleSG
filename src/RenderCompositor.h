#pragma once

#include "Result.h"

class RenderCompositor
{
public:
    RenderCompositor(const RenderCompositor&) = delete;
    RenderCompositor& operator=(const RenderCompositor&) = delete;
    RenderCompositor(RenderCompositor&&) = delete;
    RenderCompositor& operator=(RenderCompositor&&) = delete;

    virtual ~RenderCompositor() = 0;

    virtual Result<> BeginFrame() = 0;

    virtual Result<> EndFrame() = 0;

protected:
    RenderCompositor() = default;
};

inline RenderCompositor::~RenderCompositor()  = default;