#pragma once

#include "Model.h"
#include <string_view>

class GpuDevice;

class CgltfModelLoader final
{
public:
    static Result<> LoadModel(GpuDevice* gpuDevice, const std::string& path);
};