#pragma once

#include "GPUDevice.h"
#include "Material.h"

/// @brief Specification for creating a mesh.
class MeshSpec
{
public:
    const std::string Name;
    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    MaterialSpec MtlSpec;
};

/// @brief GPU representation of a mesh.
class Mesh
{
public:

    Mesh(const std::string& name,
        const VertexBuffer& vb,
        const IndexBuffer& ib,
        const uint32_t indexOffset,
        const uint32_t indexCount,
        const MaterialId materialId)
        : Name(name)
        , VtxBuffer(vb)
        , IdxBuffer(ib)
        , IndexOffset(indexOffset)
        , IndexCount(indexCount)
        , MaterialId(materialId)
    {
    }

    const std::string Name;
    const VertexBuffer VtxBuffer;
    const IndexBuffer IdxBuffer;

    const uint32_t IndexOffset;
    const uint32_t IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;
};