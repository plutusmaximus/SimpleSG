#pragma once

#include "SceneNode.h"
#include "Model.h"
#include "SceneVisitor.h"

class ModelNode : public SceneNode
{
public:

    ~ModelNode() override {}

    explicit ModelNode(RefPtr<Model> model)
        : Model(model)
    {
    }

    void Accept(SceneVisitor* visitor) override
    {
        visitor->Visit(this);
    }

    const RefPtr<Model> Model;

private:

    ModelNode() = default;
};