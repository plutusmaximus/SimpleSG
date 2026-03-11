#define __LOGGER_NAME__ "MODL"

#include "Model.h"

#include "GpuDevice.h"
#include "Log.h"

ModelSpec::ModelSpec(
    const imvector<MeshSpec>& meshSpecs,
    const imvector<TransformIndex>& meshToTransformMapping,
    const imvector<TransformNode>& transformNodes)
    : m_MeshSpecs(meshSpecs)
    , m_MeshToTransformMapping(meshToTransformMapping)
    , m_TransformNodes(transformNodes)
{
    for(size_t i = 0; i < meshToTransformMapping.size(); ++i)
    {
        MLG_ASSERT(
            meshToTransformMapping[i] < static_cast<int>(m_TransformNodes.size()),
            "Mesh instance {} has invalid mesh index {}", i, meshToTransformMapping[i]);
    }

    for(size_t i = 0; i < transformNodes.size(); ++i)
    {
        MLG_ASSERT(transformNodes[i].ParentIndex < static_cast<int>(i) ||
                       transformNodes[i].ParentIndex == kInvalidTransformIndex,
            "Transform node {} has invalid parent index {}, parent must be defined before child",
            i,
            transformNodes[i].ParentIndex);
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
                Log::Debug("Failed to destroy vertex buffer");
            }
        }

        if(m_IndexBuffer)
        {
            const auto res = m_GpuDevice->DestroyIndexBuffer(m_IndexBuffer);
            if(!res)
            {
                Log::Debug("Failed to destroy index buffer");
            }
        }

        if(m_MeshToTransformMapping)
        {
            const auto res = m_GpuDevice->DestroyReadonlyBuffer(m_MeshToTransformMapping);
            if(!res)
            {
                Log::Debug("Failed to destroy mesh to transform mapping buffer");
            }
        }

        if(m_DrawIndirectBuffer)
        {
            const auto res = m_GpuDevice->DestroyDrawIndirectBuffer(m_DrawIndirectBuffer);
            if(!res)
            {
                Log::Debug("Failed to destroy draw indirect buffer");
            }
        }
    }
}

Result<Model>
Model::Create(
    const imvector<Mesh>& meshes,
    const imvector<TransformNode>& transformNodes,
    GpuDevice* gpuDevice,
    GpuReadonlyBuffer* meshToTransformMapping,
    GpuDrawIndirectBuffer* drawIndirectBuffer,
    GpuVertexBuffer* vertexBuffer,
    GpuIndexBuffer* indexBuffer)
{
    Log::Debug(
        "Creating model with {} meshes, {} transform nodes",
        meshes.size(),
        transformNodes.size());

    for(size_t i = 0; i < transformNodes.size(); ++i)
    {
        MLG_ASSERT(transformNodes[i].ParentIndex < static_cast<int>(i) ||
                       transformNodes[i].ParentIndex == kInvalidTransformIndex,
            "Transform node {} has invalid parent index {}, parent must be defined before child",
            i,
            transformNodes[i].ParentIndex);
    }

    return Model(meshes,
        transformNodes,
        gpuDevice,
        meshToTransformMapping,
        drawIndirectBuffer,
        vertexBuffer,
        indexBuffer);
}