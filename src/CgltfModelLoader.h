#pragma once

#include "Model.h"
#include "ScenePack.h"

#include <string>

class GpuDevice;

class CgltfModelLoader final
{
public:
    static Result<ScenePack*> LoadScenePack(GpuDevice* gpuDevice, const std::string& path);
};