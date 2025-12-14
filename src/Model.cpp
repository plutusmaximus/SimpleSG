#include "Model.h"

Model::Model(std::span<Mesh> meshes)
    : Meshes(meshes.begin(), meshes.end())
{
}

Result<RefPtr<Model>>
Model::Create(std::span<Mesh> meshes)
{
    Model* model = new Model(meshes);

    expectv(model, "Error allocating model");

    return model;
}