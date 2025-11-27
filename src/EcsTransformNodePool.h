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

/// @brief Collection of nodes organized in a hierarchy.
template<>
class EcsComponentPool<TransformNode2> : public IEcsPool
{
public:

    using IndexType = EntityId::ValueType;
    static constexpr IndexType InvalidIndex = EntityId::InvalidValue;

    bool Add(const EntityId eid)
    {
        return Add(eid, TransformNode2{});
    }

    /// @brief Adds a new sub-assembly node under the given parent.
    /// The new node is inserted immediately after its parent and any existing children.
    /// This maintains a depth-first ordering of the hierarchy.
    /// Child nodes appear in the collection in reverse order of addition.
    bool Add(const EntityId eid, const TransformNode2& node)
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

    /// @brief Removes the sub-assembly node with the given entity ID, along with all its children.
    void Remove(const EntityId eid)
    {
        const int eidIdx = IndexOf(eid);
        if(eidIdx == -1)
        {
            return;
        }

        const size_t boundIdx = SubAssemblyBounds(eid);

        //Invalidate indexes for all nodes being removed
        for(size_t idx = eidIdx; idx < boundIdx; ++idx)
        {
            const auto deletedEid = m_EntityIds[idx];
            m_Index[deletedEid.Value()] = -1;
        }

        // Remove starting from the top of the sub-assembly up to but not include boundIdx.
        m_Components.erase(m_Components.begin() + eidIdx, m_Components.begin() + boundIdx);
        m_EntityIds.erase(m_EntityIds.begin() + eidIdx, m_EntityIds.begin() + boundIdx);

        //Remap indexes for all subsequent nodes
        const size_t size = m_Components.size();
        for(size_t i = eidIdx; i < size; ++i)
        {
            const EntityId currentEid = m_EntityIds[i];
            m_Index[currentEid.Value()] = static_cast<IndexType>(i);
        }
    }

    TransformNode2* Get(const EntityId eid)
    {
        const IndexType idx = IndexOf(eid);
        return (InvalidIndex != idx) ? &m_Components[idx] : nullptr;
    }

    const TransformNode2* Get(const EntityId eid) const
    {
        const IndexType idx = IndexOf(eid);
        return (InvalidIndex != idx) ? &m_Components[idx] : nullptr;
    }

    bool Has(const EntityId eid) const
    {
        return eid.Value() < m_Index.size() && m_Index[eid.Value()] != EntityId::InvalidValue;
    }

    size_t size() const
    {
        return m_Components.size();
    }

    using Iterator = typename std::vector<EntityId>::iterator;
    using ConstIterator = typename std::vector<EntityId>::const_iterator;

    Iterator begin()
    {
        return m_EntityIds.begin();
    }

    Iterator end()
    {
        return m_EntityIds.end();
    }

    ConstIterator begin() const
    {
        return m_EntityIds.begin();
    }

    ConstIterator end() const
    {
        return m_EntityIds.end();
    }

private:

    /// @brief Returns the index of the node with the given entity ID, or -1 if not found.
    int IndexOf(const EntityId id) const
    {
        const auto value = id.Value();

        return value < static_cast<int>(m_Index.size())
        ? m_Index[value]
        : -1;
    }

    /// @brief Returns an iterator to one past the end of the sub-assembly rooted at the given parent.
    size_t SubAssemblyBounds(const EntityId parentId)
    {
        const int parentIdx = IndexOf(parentId);
        if(parentIdx == -1)
        {
            return m_Components.size();
        }

        const size_t size = m_Components.size();
        size_t childIdx = parentIdx + 1;

        while(size != childIdx)
        {
            if(m_Components[childIdx].ParentId != parentId)
            {
                break;
            }

            childIdx = SubAssemblyBounds(m_EntityIds[childIdx]);
        }

        return childIdx;
    }

    // Mapping from EntityId to index in the component vector.
    std::vector<IndexType> m_Index;
    // Entity IDs indexed by m_Index.
    std::vector<EntityId> m_EntityIds;
    // Components indexed by m_Index.
    std::vector<TransformNode2> m_Components;
};