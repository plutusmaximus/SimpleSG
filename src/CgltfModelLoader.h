#pragma once

#include "Result.h"
#include "ScenePack.h"

#include <string>

namespace wgpu
{
class Device;
}

class CgltfModelLoader final
{
public:
    static Result<ScenePack*> LoadScenePack(wgpu::Device& device, const std::string& path);
};