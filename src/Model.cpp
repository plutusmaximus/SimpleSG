#define __LOGGER_NAME__ "MODL"

#include "Model.h"

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

Result<RefPtr<Model>>
Model::Create(const imvector<Mesh>& meshes, const imvector<MeshInstance>& meshInstances, const imvector<TransformNode>& transformNodes)
{
    Model* model = new Model(meshes, meshInstances, transformNodes);
    expectv(model, "Error allocating model");

    return model;
}

Model::Model(const imvector<Mesh>& meshes, const imvector<MeshInstance>& meshInstances, const imvector<TransformNode>& transformNodes)
    : Meshes(meshes)
    , MeshInstances(meshInstances)
    , TransformNodes(transformNodes)
{
    logDebug(
        "Creating model with {} meshes, {} mesh instances and {} transform nodes",
        Meshes.size(),
        MeshInstances.size(),
        TransformNodes.size());

    for(size_t i = 0; i < MeshInstances.size(); ++i)
    {
        logDebug(
            "  Mesh instance {}: mesh index {}({}), node index {}",
            i,
            MeshInstances[i].MeshIndex,
            Meshes[MeshInstances[i].MeshIndex].Name,
            MeshInstances[i].NodeIndex);

        const MeshInstance& meshInstance = MeshInstances[i];
        eassert(
            meshInstance.MeshIndex >= 0 &&
            meshInstance.MeshIndex < static_cast<int>(Meshes.size()),
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
};