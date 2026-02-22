#define __LOGGER_NAME__ "MODL"

#include "Model.h"

#include "GpuDevice.h"
#include "Logging.h"

ModelSpec::ModelSpec(
    const imvector<MeshSpec>& meshSpecs,
    const imvector<MeshInstance>& meshInstances,
    const imvector<TransformNode>& transformNodes)
    : m_MeshSpecs(meshSpecs)
    , m_MeshInstances(meshInstances)
    , m_TransformNodes(transformNodes)
{
    for(size_t i = 0; i < m_MeshInstances.size(); ++i)
    {
        eassert(
            m_MeshInstances[i].MeshIndex >= 0 &&
            m_MeshInstances[i].MeshIndex < static_cast<int>(m_MeshSpecs.size()),
            "Mesh instance {} has invalid mesh index {}", i, m_MeshInstances[i].MeshIndex);

        eassert(
            m_MeshInstances[i].NodeIndex >= 0 &&
            m_MeshInstances[i].NodeIndex < static_cast<int>(m_TransformNodes.size()),
            "Mesh instance {} has invalid node index {}", i, m_MeshInstances[i].NodeIndex);
    }

    for(size_t i = 0; i < m_TransformNodes.size(); ++i)
    {
        eassert(m_TransformNodes[i].ParentIndex < static_cast<int>(i),
            "Transform node {} has invalid parent index {}, parent must be defined before child",
            i, m_TransformNodes[i].ParentIndex);
    }
}

Model::~Model()
{
    if(m_GpuDevice)
    {
        if(m_VertexBuffer)
        {
            const auto res = m_GpuDevice->DestroyVertexBuffer(m_VertexBuffer);
            if(!res)
            {
                logDebug("Failed to destroy vertex buffer: {}", res.error());
            }
        }

        if(m_IndexBuffer)
        {
            const auto res = m_GpuDevice->DestroyIndexBuffer(m_IndexBuffer);
            if(!res)
            {
                logDebug("Failed to destroy index buffer: {}", res.error());
            }
        }
    }
}

Result<Model>
Model::Create(
    const imvector<Mesh>& meshes,
    const imvector<MeshInstance>& meshInstances,
    const imvector<TransformNode>& transformNodes,
    GpuDevice* gpuDevice,
    GpuVertexBuffer* vertexBuffer,
    GpuIndexBuffer* indexBuffer)
{
    logDebug(
        "Creating model with {} meshes, {} mesh instances and {} transform nodes",
        meshes.size(),
        meshInstances.size(),
        transformNodes.size());

    for(size_t i = 0; i < meshInstances.size(); ++i)
    {
        logDebug(
            "  Mesh instance {}: mesh index {}({}), node index {}",
            i,
            meshInstances[i].MeshIndex,
            meshes[meshInstances[i].MeshIndex].GetName(),
            meshInstances[i].NodeIndex);

        eassert(
            meshInstances[i].MeshIndex >= 0 &&
            meshInstances[i].MeshIndex < static_cast<int>(meshes.size()),
            "Mesh instance {} has invalid mesh index {}", i, meshInstances[i].MeshIndex);

        eassert(
            meshInstances[i].NodeIndex >= 0 &&
            meshInstances[i].NodeIndex < static_cast<int>(transformNodes.size()),
            "Mesh instance {} has invalid node index {}", i, meshInstances[i].NodeIndex);
    }

    for(size_t i = 0; i < transformNodes.size(); ++i)
    {
        eassert(transformNodes[i].ParentIndex < static_cast<int>(i),
            "Transform node {} has invalid parent index {}, parent must be defined before child",
            i, transformNodes[i].ParentIndex);
    }

    return Model(meshes, meshInstances, transformNodes, gpuDevice, vertexBuffer, indexBuffer);
}