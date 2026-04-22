#pragma once

#include "Result.h"
#include "PropKit.h"
#include "Scene.h"

#include <string>

class GltfLoader final
{
public:

    static Result<> LoadPropKit(const std::string& path, PropKitDef& outPropKit, SceneDef& outSceneDef);
};