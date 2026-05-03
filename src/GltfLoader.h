#pragma once

#include "Result.h"

#include <string>

struct PropKitDef;
struct LevelDef;

class GltfLoader final
{
public:

    static Result<> Load(const std::string& path, PropKitDef& outPropKit, LevelDef& outLevelDef);
};