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
        RefPtr<GpuVertexBuffer> vb,
        RefPtr<GpuIndexBuffer> ib,
        const uint32_t indexCount,
        const Material& material)
        : Name(name)
        , VtxBuffer(vb)
        , IdxBuffer(ib)
        , IndexCount(indexCount)
        , Material(material)
    {
    }

    const std::string Name;
    const RefPtr<GpuVertexBuffer> VtxBuffer;
    const RefPtr<GpuIndexBuffer> IdxBuffer;
    const uint32_t IndexCount;
    const Material Material;

private:

    Mesh() = delete;
};