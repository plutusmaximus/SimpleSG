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
        const GpuVertexBuffer* vb,
        const GpuIndexBuffer* ib,
        const unsigned indexCount,
        const unsigned vertexOffset,
        const unsigned indexOffset,
        const Material& material,
        GpuMaterial* gpuMaterial)
        : m_Name(name)
        , m_VtxBuffer(vb)
        , m_IdxBuffer(ib)
        , m_IndexCount(indexCount)
        , m_Material(material)
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
    const Material& GetMaterial() const { return m_Material; }
    GpuMaterial* GetGpuMaterial() const { return m_GpuMaterial; }
    const GpuVertexBuffer* GetGpuVertexBuffer() const { return m_VtxBuffer; }
    const GpuIndexBuffer* GetGpuIndexBuffer() const { return m_IdxBuffer; }

private:

    Mesh() = delete;

    imstring m_Name;
    const GpuVertexBuffer* m_VtxBuffer;
    const GpuIndexBuffer* m_IdxBuffer;
    unsigned m_IndexOffset;
    unsigned m_VertexOffset;
    unsigned m_IndexCount;
    Material m_Material;
    GpuMaterial* m_GpuMaterial{ nullptr };
};