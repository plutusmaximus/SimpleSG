#define __LOGGER_NAME__ "MODL"

#include "Model.h"

#include "GpuDevice.h"
#include "Logging.h"

ModelSpec::ModelSpec(
    const imvector<MeshSpec>& meshSpecs,
    const imvector<MeshInstance>& meshInstances,
    const imvector<TransformNode>& transformNodes)
    : MeshSpecs(meshSpecs)
    , MeshInstances(meshInstances)
    , TransformNodes(transformNodes)
{
    for(size_t i = 0; i < MeshInstances.size(); ++i)
    {
        const MeshInstance& meshInstance = MeshInstances[i];
        eassert(
            meshInstance.MeshIndex >= 0 &&
            meshInstance.MeshIndex < static_cast<int>(MeshSpecs.size()),
            "Mesh instance {} has invalid mesh index {}", i, meshInstance.MeshIndex);

        eassert(
            meshInstance.NodeIndex >= 0 &&
            meshInstance.NodeIndex < static_cast<int>(TransformNodes.size()),
            "Mesh instance {} has invalid node index {}", i, meshInstance.NodeIndex);
    }

    for(size_t i = 0; i < TransformNodes.size(); ++i)
    {
        const TransformNode& node = TransformNodes[i];
        eassert(node.ParentIndex < static_cast<int>(i),
            "Transform node {} has invalid parent index {}, parent must be defined before child",
            i, node.ParentIndex);
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

        const MeshInstance& meshInstance = meshInstances[i];
        eassert(
            meshInstance.MeshIndex >= 0 &&
            meshInstance.MeshIndex < static_cast<int>(meshes.size()),
            "Mesh instance {} has invalid mesh index {}", i, meshInstance.MeshIndex);

        eassert(
            meshInstance.NodeIndex >= 0 &&
            meshInstance.NodeIndex < static_cast<int>(transformNodes.size()),
            "Mesh instance {} has invalid node index {}", i, meshInstance.NodeIndex);
    }

    for(size_t i = 0; i < transformNodes.size(); ++i)
    {
        const TransformNode& node = transformNodes[i];
        eassert(node.ParentIndex < static_cast<int>(i),
            "Transform node {} has invalid parent index {}, parent must be defined before child",
            i, node.ParentIndex);
    }

    return Model(meshes, meshInstances, transformNodes, gpuDevice, vertexBuffer, indexBuffer);
}