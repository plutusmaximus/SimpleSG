#pragma once

#include "Result.h"
#include "SceneKit.h"

#include <string>

class GltfLoader final
{
public:

    static Result<> LoadSceneKit(const std::string& path, SceneKitSourceData& outSceneKit);
};