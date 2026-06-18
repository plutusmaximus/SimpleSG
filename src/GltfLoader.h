#pragma once

#include "Result.h"

#include <string>

struct PropKitDef;
struct LevelDef;

class GltfLoader final
{
public:

    GltfLoader() = delete;
    ~GltfLoader() = delete;
    GltfLoader(const GltfLoader&) = delete;
    GltfLoader& operator=(const GltfLoader&) = delete;
    GltfLoader(GltfLoader&&) = delete;
    GltfLoader& operator=(GltfLoader&&) = delete;

    static Result<> Load(const std::string& path, PropKitDef& outPropKit, LevelDef& outLevelDef);
};