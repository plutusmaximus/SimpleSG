#pragma once

#include "Result.h"
#include "SemanticInteger.h"
#include "VecMath.h"

#include <string>
#include <span>
#include <unordered_map>
#include <vector>

struct LevelNodeIndexTag {};
using LevelNodeIndex = SemanticInteger<LevelNodeIndexTag>;

class PropKit;

struct LevelNode
{
    TrsTransformf Transform;
    LevelNodeIndex FirstChildIndex{ LevelNodeIndex::INVALID };
    uint32_t ChildCount{ 0 };
};

struct LevelNodeDef
{
    std::string Name;
    std::string AssemblyName;
    TrsTransformf Transform;
};

struct LevelDef
{
    std::vector<LevelNodeDef> NodeDefs;
};

class Level
{
public:
    static Result<> Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel);

    Level() = default;
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&& other) = default;
    Level& operator=(Level&& other) = default;

private:
    Level(const PropKit* propKit,
        std::vector<LevelNode>&& nodes,
        std::unordered_map<std::string, size_t>&& nodeNameToIndex)
        : m_PropKit(propKit),
          m_Nodes(std::move(nodes)),
          m_NodeNameToIndex(std::move(nodeNameToIndex))
    {
    }

    const PropKit* m_PropKit = nullptr;
    std::vector<LevelNode> m_Nodes;
    std::unordered_map<std::string, size_t> m_NodeNameToIndex;
};