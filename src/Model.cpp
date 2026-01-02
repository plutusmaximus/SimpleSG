#include "Model.h"

ModelSpec::ModelSpec(
    std::vector<MeshSpec>&& meshSpecs,
    std::vector<MeshInstance>&& meshInstances)
    : MeshSpecs(std::move(meshSpecs))
    , MeshInstances(std::move(meshInstances))
{
    for(size_t i = 0; i < MeshInstances.size(); ++i)
    {
        const MeshInstance& meshInstance = MeshInstances[i];
        eassert(
            meshInstance.MeshIndex >= 0 &&
            meshInstance.MeshIndex < static_cast<int>(MeshSpecs.size()),
            "Mesh instance {} has invalid mesh index {}", i, meshInstance.MeshIndex);

        eassert(
            meshInstance.ParentIndex < static_cast<int>(MeshInstances.size()),
            "Mesh instance {} has invalid parent index {}", i, meshInstance.ParentIndex);
    }
}

Result<RefPtr<Model>>
Model::Create(std::vector<Mesh>&& meshes, std::vector<MeshInstance>&& meshInstances)
{
    Model* model = new Model(
        std::forward<std::vector<Mesh>>(meshes),
        std::forward<std::vector<MeshInstance>>(meshInstances));

    expectv(model, "Error allocating model");

    return model;
}

Model::Model(std::vector<Mesh>&& meshes, std::vector<MeshInstance>&& meshInstances)
    : Meshes(std::move(meshes))
    , MeshInstances(std::move(meshInstances))
{
    for(size_t i = 0; i < MeshInstances.size(); ++i)
    {
        const MeshInstance& meshInstance = MeshInstances[i];
        eassert(
            meshInstance.MeshIndex >= 0 &&
            meshInstance.MeshIndex < static_cast<int>(Meshes.size()),
            "Mesh instance {} has invalid mesh index {}", i, meshInstance.MeshIndex);

        eassert(
            meshInstance.ParentIndex < static_cast<int>(MeshInstances.size()),
            "Mesh instance {} has invalid parent index {}", i, meshInstance.ParentIndex);
    }
};