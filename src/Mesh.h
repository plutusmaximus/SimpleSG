#pragma once

#include "GPUDevice.h"
#include "Material.h"
#include "Vertex.h"

#include <string>
#include <vector>

/// @brief Specification for creating a mesh.
class MeshSpec
{
public:
    const std::string Name;
    const std::vector<Vertex> Vertices;
    const std::vector<VertexIndex> Indices;
    const MaterialSpec MtlSpec;
};

/// @brief GPU representation of a mesh.
class Mesh
{
public:

    Mesh(const std::string& name,
        const GpuVertexBuffer& vb,
        const GpuIndexBuffer& ib,
        const uint32_t indexCount,
        const MaterialId materialId)
        : Name(name)
        , VtxBuffer(vb)
        , IdxBuffer(ib)
        , IndexCount(indexCount)
        , MaterialId(materialId)
    {
    }

    const std::string Name;
    const GpuVertexBuffer VtxBuffer;
    const GpuIndexBuffer IdxBuffer;
    const uint32_t IndexCount;
    const MaterialId MaterialId;

private:

    Mesh() = delete;
};