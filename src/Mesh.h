#pragma once

#include "GPUDevice.h"
#include "Material.h"

class MeshSpec
{
public:
    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    MaterialSpec MtlSpec;
};

class Mesh
{
public:

    Mesh(const VertexBuffer& vb,
        const IndexBuffer& ib,
        const uint32_t indexOffset,
        const uint32_t indexCount,
        const ::MaterialId materialId)
        : VtxBuffer(vb)
        , IdxBuffer(ib)
        , IndexOffset(indexOffset)
        , IndexCount(indexCount)
        , MaterialId(materialId)
    {
    }

    const VertexBuffer VtxBuffer;
    const IndexBuffer IdxBuffer;

    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;
};