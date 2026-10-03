#pragma once

#include "Result.h"

#include <string_view>

struct SceneDef;

class GltfLoader final
{
public:

    GltfLoader() = delete;
    ~GltfLoader() = delete;
    GltfLoader(const GltfLoader&) = delete;
    GltfLoader& operator=(const GltfLoader&) = delete;
    GltfLoader(GltfLoader&&) = delete;
    GltfLoader& operator=(GltfLoader&&) = delete;

    static Result<SceneDef> Load(const std::string_view path);
};