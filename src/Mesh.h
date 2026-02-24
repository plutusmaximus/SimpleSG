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
        const GpuVertexBuffer::Subrange& vb,
        const GpuIndexBuffer::Subrange& ib,
        const uint32_t indexCount,
        const Material& material,
        GpuMaterial* gpuMaterial)
        : m_Name(name)
        , m_VtxBuffer(vb)
        , m_IdxBuffer(ib)
        , m_IndexCount(indexCount)
        , m_Material(material)
        , m_GpuMaterial(gpuMaterial)
    {
    }

    Mesh(const Mesh& other) = delete;
    Mesh& operator=(const Mesh& other) = delete;
    Mesh(Mesh&& other) = default;
    Mesh& operator=(Mesh&& other) = default;

    const imstring& GetName() const { return m_Name; }
    const GpuVertexBuffer::Subrange& GetVertexBuffer() const { return m_VtxBuffer; }
    const GpuIndexBuffer::Subrange& GetIndexBuffer() const { return m_IdxBuffer; }
    uint32_t GetIndexCount() const { return m_IndexCount; }
    const Material& GetMaterial() const { return m_Material; }
    GpuMaterial* GetGpuMaterial() const { return m_GpuMaterial; }

private:

    Mesh() = delete;

    imstring m_Name;
    GpuVertexBuffer::Subrange m_VtxBuffer;
    GpuIndexBuffer::Subrange m_IdxBuffer;
    uint32_t m_IndexCount;
    Material m_Material;
    GpuMaterial* m_GpuMaterial{ nullptr };
};