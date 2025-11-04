#pragma once

#include "Material.h"
#include "RefCount.h"
#include "Error.h"

class MeshSpec
{
public:
    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    MaterialSpec MtlSpec;
};

class VertexBuffer
{
public:

    VertexBuffer() {}

    virtual ~VertexBuffer() = 0 {}

    IMPLEMENT_REFCOUNT(VertexBuffer);
};

class IndexBuffer
{
public:

    IndexBuffer() {}

    virtual ~IndexBuffer() = 0 {}

    IMPLEMENT_REFCOUNT(IndexBuffer);
};

class Mesh
{
public:

    static Result<RefPtr<Mesh>> Create(
        VertexBuffer* vb,
        IndexBuffer* ib,
        const uint32_t indexOffset,
        const uint32_t indexCount,
        const MaterialId materialId)
    {
        Mesh* mesh = new Mesh(vb, ib, indexOffset, indexCount, materialId);

        expectv(mesh, "Error allocating mesh");

        return mesh;
    }

    RefPtr<VertexBuffer> VtxBuffer;
    RefPtr<IndexBuffer> IdxBuffer;

    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;

    Mesh(
        VertexBuffer* vb,
        IndexBuffer* ib,
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

    IMPLEMENT_REFCOUNT(Mesh);
};