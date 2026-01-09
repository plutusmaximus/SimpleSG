#pragma once

#include "GPUDevice.h"
#include "Material.h"
#include "Vertex.h"
#include "imvector.h"

#include <string>

/// @brief Specification for creating a mesh.
class MeshSpec
{
public:
    const std::string Name;
    const imvector<Vertex> Vertices;
    const imvector<VertexIndex> Indices;
    const MaterialSpec MtlSpec;
};

/// @brief GPU representation of a mesh.
class Mesh
{
public:

    Mesh(std::string_view name,
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