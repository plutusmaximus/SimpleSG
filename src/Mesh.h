#pragma once

#include "GPUDevice.h"
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

class Mesh
{
public:

    static Result<RefPtr<Mesh>> Create(
        const VertexBuffer& vb,
        const IndexBuffer& ib,
        const uint32_t indexOffset,
        const uint32_t indexCount,
        const MaterialId materialId)
    {
        Mesh* mesh = new Mesh(vb, ib, indexOffset, indexCount, materialId);

        expectv(mesh, "Error allocating mesh");

        return mesh;
    }

    const VertexBuffer VtxBuffer;
    const IndexBuffer IdxBuffer;

    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;

    Mesh(
        const VertexBuffer& vb,
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

    IMPLEMENT_REFCOUNT(Mesh);
};