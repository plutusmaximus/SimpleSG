#pragma once

#include "ECS.h"
#include "VecMath.h"

class ChildTransform
{
public:

    EntityId ParentId;
    TrsTransformf LocalTransform;

    /// @brief Implements less-than operator for sorting.
    /// Parent entities are always less than their children.
    bool operator<(const ChildTransform& that) const
    {
        return ParentId < that.ParentId;
    }
};

/// @brief Specialized Add method for ChildTransform to maintain hierarchy ordering.
/// Adds a new sub-assembly node under the given parent.
/// The new node is inserted immediately after its parent and any existing children.
/// This maintains a depth-first ordering of the hierarchy.
/// Child nodes appear in the collection in reverse order of addition.
bool EcsComponentPool<ChildTransform>::Add(const EntityId eid, const ChildTransform& child);

/// @brief Specialized Remove method for ChildTransform to remove entire sub-assemblies.
/// Removes the sub-assembly node with the given entity ID, along with all its children.
void EcsComponentPool<ChildTransform>::Remove(const EntityId eid);