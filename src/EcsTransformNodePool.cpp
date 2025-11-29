#include "EcsTransformNodePool.h"

bool
EcsComponentPool<TransformNode2>::Add(const EntityId eid, const TransformNode2& node)
{
    const EntityId parentId = node.ParentId;

    if(!everify(eid.IsValid(), "EntityId must be valid"))
    {
        return false;
    }

    if(!everify(!Has(eid), "Entity ID already in collection"))
    {
        return false;
    }

    if(!everify(eid != parentId, "Entity cannot be its own parent"))
    {
        return false;
    }

    //TODO(KB) - check for cycles in parentage.
    //TODO(KB) - optimization: when removing an item with no children
    // check to see if the last item in the collection is also childless
    // and if so just swap-remove to avoid shifting elements.
    //TODO(KB) - optimization: when removing a child item shift all subsequent
    // children (and children of children) up in one go rather than one at a time.
    // Then check if the last item in the collection is childless and swap-remove
    // into the vacated slot.

    if(eid.Value() >= static_cast<int>(m_Index.size()))
    {
        m_Index.resize(eid.Value() + 1, -1);
    }

    if(!parentId.IsValid())
    {
        // No parent, add as top-level node
        m_Index[eid.Value()] = static_cast<int>(m_Components.size());
        m_Components.emplace_back(node);
        m_EntityIds.emplace_back(eid);
        return true;
    }

    const int idxOfParent = IndexOf(parentId);
    if(!everify(idxOfParent != -1, "Parent ID not found in collection"))
    {
        // Parent not found, which shouldn't happen in a valid hierarchy
        return false;
    }

    int idx = idxOfParent + 1;
    m_Components.emplace(m_Components.begin() + idx, node);
    auto it = m_EntityIds.emplace(m_EntityIds.begin() + idx, eid);

    //Update indexes for inserted ID and all subsequent IDs
    for(const auto endIt = m_EntityIds.end(); endIt != it; ++it, ++idx)
    {
        m_Index[(*it).Value()] = idx;
    }

    return true;
}

void
EcsComponentPool<TransformNode2>::Remove(const EntityId eid)
{
    const IndexType eidIdx = IndexOf(eid);
    if(!everify(eidIdx != InvalidIndex, "Entity ID not found"))
    {
        return;
    }

    /// @brief Recursively find the bounds of the sub-assembly (parent and all descendants).
    auto subAssemblyBounds = [this](this auto self, const EntityId parentId) -> IndexType
    {
        const IndexType parentIdx = IndexOf(parentId);
        eassert(parentIdx != -1, "Parent ID not found");

        const size_t endIdx = this->size();
        IndexType childIdx = parentIdx + 1;

        while(childIdx < endIdx)
        {
            const auto& [childId, childNode] = (*this)[childIdx];
            if(childNode.ParentId != parentId)
            {
                break;
            }

            childIdx = self(childId);
        }

        return childIdx;
    };

    const IndexType boundIdx = subAssemblyBounds(eid);

    //Invalidate indexes for all nodes being removed
    for(IndexType idx = eidIdx; idx < boundIdx; ++idx)
    {
        const auto deletedEid = m_EntityIds[idx];
        m_Index[deletedEid.Value()] = -1;
    }

    // Remove starting from the top of the sub-assembly up to but not include boundIdx.
    m_Components.erase(m_Components.begin() + eidIdx, m_Components.begin() + boundIdx);
    m_EntityIds.erase(m_EntityIds.begin() + eidIdx, m_EntityIds.begin() + boundIdx);

    //Remap indexes for all subsequent nodes
    const size_t size = m_Components.size();
    for(IndexType i = eidIdx; i < size; ++i)
    {
        const EntityId currentEid = m_EntityIds[i];
        m_Index[currentEid.Value()] = static_cast<IndexType>(i);
    }
}