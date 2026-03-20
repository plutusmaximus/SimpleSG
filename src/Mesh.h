#pragma once

#include "GpuDevice.h"
#include "Material.h"
#include "Vertex.h"
#include "imvector.h"
#include "imstring.h"

/// @brief Specification for creating a mesh.
class MeshSpec
{
public:
    const imstring Name;
    const imvector<Vertex> Vertices;
    const imvector<VertexIndex> Indices;
    const MaterialSpec MtlSpec;
};

/// @brief GPU representation of a mesh.
class Mesh
{
public:

    Mesh(const imstring& name,
        const unsigned indexCount,
        const unsigned vertexOffset,
        const unsigned indexOffset,
        GpuMaterial* gpuMaterial)
        : m_Name(name)
        , m_IndexCount(indexCount)
        , m_GpuMaterial(gpuMaterial)
        , m_IndexOffset(indexOffset)
        , m_VertexOffset(vertexOffset)
    {
    }

    Mesh(const Mesh& other) = delete;
    Mesh& operator=(const Mesh& other) = delete;
    Mesh(Mesh&& other) = default;
    Mesh& operator=(Mesh&& other) = default;

    const imstring& GetName() const { return m_Name; }
    unsigned GetIndexOffset() const { return m_IndexOffset; }
    unsigned GetVertexOffset() const { return m_VertexOffset; }
    unsigned GetIndexCount() const { return m_IndexCount; }
    GpuMaterial* GetGpuMaterial() const { return m_GpuMaterial; }

private:

    Mesh() = delete;

    imstring m_Name;
    unsigned m_IndexOffset;
    unsigned m_VertexOffset;
    unsigned m_IndexCount;
    GpuMaterial* m_GpuMaterial{ nullptr };
};