#pragma once

#include "MaterialDb.h"

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
        SdlVertexBuffer vb,
        SdlIndexBuffer ib,
        const int indexOffset,
        const int indexCount,
        const MaterialId materialId)
    {
        return new Mesh(vb, ib, indexOffset, indexCount, materialId);
    }

    SdlVertexBuffer VertexBuffer;
    SdlIndexBuffer IndexBuffer;

    const int IndexOffset;
    const int IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;

    Mesh(
        SdlVertexBuffer vb,
        SdlIndexBuffer ib,
        const int indexOffset,
        const int indexCount,
        const ::MaterialId materialId)
        : VertexBuffer(vb)
        , IndexBuffer(ib)
        , IndexOffset(indexOffset)
        , IndexCount(indexCount)
        , MaterialId(materialId)
    {
    }

    IMPLEMENT_NON_COPYABLE(Mesh);

    IMPLEMENT_REFCOUNT(Mesh);
};