#pragma once

#include "Material.h"

#include "GPUDevice.h"

struct UV2
{
    float u, v;
};

struct Vertex
{
    Vec3f pos;
    Vec3f normal;
    UV2 uv;
};

class Mesh
{
public:

    static RefPtr<Mesh> Create(
        VertexBuffer vb,
        IndexBuffer ib,
        const int indexOffset,
        const int indexCount,
        const MaterialId materialId)
    {
        return new Mesh(vb, ib, indexOffset, indexCount, materialId);
    }

    VertexBuffer VtxBuffer;
    IndexBuffer IdxBuffer;

    const int IndexOffset;
    const int IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;

    Mesh(
        VertexBuffer vb,
        IndexBuffer ib,
        const int indexOffset,
        const int indexCount,
        const ::MaterialId materialId)
        : VtxBuffer(vb)
        , IdxBuffer(ib)
        , IndexOffset(indexOffset)
        , IndexCount(indexCount)
        , MaterialId(materialId)
    {
    }

    IMPLEMENT_REFCOUNT(Mesh);
};