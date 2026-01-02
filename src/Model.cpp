#include "Model.h"

Result<RefPtr<Model>>
Model::Create(std::vector<Mesh>&& meshes)
{
    Model* model = new Model(std::forward<std::vector<Mesh>>(meshes));

    expectv(model, "Error allocating model");

    return model;
}