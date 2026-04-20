#pragma once

#include "Result.h"
#include "PropKit.h"

#include <string>

class GltfLoader final
{
public:

    static Result<> LoadPropKit(const std::string& path, PropKitSourceData& outPropKit);
};