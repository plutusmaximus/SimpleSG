#pragma once

#include "ECS.h"
#include "VecMath.h"

class TransformNode2
{
public:

    EntityId ParentId;
    TrsTransformf LocalTransform;

    /// @brief Implements less-than operator for sorting.
    /// Parent entities are always less than their children.
    bool operator<(const TransformNode2& that) const
    {
        return ParentId < that.ParentId;
    }
};

/// @brief Specialized Add method for TransformNode2 to maintain hierarchy ordering.
/// Adds a new sub-assembly node under the given parent.
/// The new node is inserted immediately after its parent and any existing children.
/// This maintains a depth-first ordering of the hierarchy.
/// Child nodes appear in the collection in reverse order of addition.
bool EcsComponentPool<TransformNode2>::Add(const EntityId eid, const TransformNode2& node);

/// @brief Specialized Remove method for TransformNode2 to remove entire sub-assemblies.
/// Removes the sub-assembly node with the given entity ID, along with all its children.
void EcsComponentPool<TransformNode2>::Remove(const EntityId eid);