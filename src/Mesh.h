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
        VertexBuffer::Subrange&& vb,
        IndexBuffer::Subrange&& ib,
        const uint32_t indexCount,
        const Material& material)
        : m_Name(name)
        , m_VtxBuffer(std::move(vb))
        , m_IdxBuffer(std::move(ib))
        , m_IndexCount(indexCount)
        , m_Material(material)
    {
    }

    Mesh(const Mesh& other) = delete;
    Mesh& operator=(const Mesh& other) = delete;
    Mesh(Mesh&& other) = default;
    Mesh& operator=(Mesh&& other) = default;

    const imstring& GetName() const { return m_Name; }
    const VertexBuffer::Subrange& GetVertexBuffer() const { return m_VtxBuffer; }
    const IndexBuffer::Subrange& GetIndexBuffer() const { return m_IdxBuffer; }
    uint32_t GetIndexCount() const { return m_IndexCount; }
    const Material& GetMaterial() const { return m_Material; }

private:

    Mesh() = delete;

    imstring m_Name;
    VertexBuffer::Subrange m_VtxBuffer;
    IndexBuffer::Subrange m_IdxBuffer;
    uint32_t m_IndexCount;
    Material m_Material;
};