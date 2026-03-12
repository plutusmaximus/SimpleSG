#pragma once

#include "Model.h"
#include <string_view>

class CgltfModelLoader final
{
public:
    static Result<> LoadModel(const std::string& path);
};