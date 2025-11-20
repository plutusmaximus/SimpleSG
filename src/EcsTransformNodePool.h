#pragma once

#include "ECS.h"

class Part
{
public:

    EntityId Id;
    EntityId ParentId;
    float Transform[16];

    /// @brief Implements less-than operator for sorting.
    /// Parent entities are always less than their children.
    bool operator<(const Part& that) const
    {
        return ParentId < that.ParentId;
    }
};

/// @brief Collection of Parts organized in a hierarchy.
//template<>
//class EcsComponentPool<Part> : public IEcsPool
class AssemblyCollection
{
public:

    using IndexType = int;
    static constexpr IndexType InvalidIndex = -1;

    void Add(const EntityId eid)
    {
        Add(eid, Part{ .Id = eid });
    }

    /// @brief Adds a new sub-assembly part under the given parent.
    /// The new part is inserted immediately after its parent and any existing children.
    /// This maintains a depth-first ordering of the hierarchy.
    /// Child parts appear in the collection in reverse order of addition.
    void Add(const EntityId eid, const Part& part)
    {
        const EntityId parentId = part.ParentId;

        if(!everify(eid.IsValid() && "EntityId must be valid"))
        {
            return;
        }

        if(!everify(!Has(eid) && "Entity ID already in collection"))
        {
            return;
        }

        if(!everify(eid != parentId && "Entity cannot be its own parent"))
        {
            return;
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
            // No parent, add as top-level part
            m_Index[eid.Value()] = static_cast<int>(m_Components.size());
            m_Components.emplace_back(part);
            return;
        }

        const int idxOfParent = IndexOf(parentId);
        if(!everify(idxOfParent != -1) && "Parent ID not found in collection")
        {
            // Parent not found, which shouldn't happen in a valid hierarchy
            return;
        }

        int idx = idxOfParent + 1;
        auto it = m_Components.emplace(m_Components.begin() + idx, part);

        //Update indexes for inserted part and all subsequent parts
        for(const auto endIt = m_Components.end(); endIt != it; ++it, ++idx)
        {
            m_Index[(*it).Id.Value()] = idx;
        }
    }

    /// @brief Removes the sub-assembly part with the given entity ID, along with all its children.
    void Remove(const EntityId eid)
    {
        const int idx = IndexOf(eid);
        if(idx == -1)
        {
            return;
        }

        auto itFirst = m_Components.begin() + idx;
        auto itBound = SubAssemblyBounds(eid);

        for(auto it = itFirst; it != itBound; ++it)
        {
            m_Index[it->Id.Value()] = -1;
        }

        // Remove starting from itFirst up to but not include itBound.
        m_Components.erase(itFirst, itBound);
        const size_t numRemoved = static_cast<size_t>(itBound - itFirst);

        for(size_t i = idx; i < m_Components.size(); ++i)
        {
            m_Index[m_Components[i].Id.Value()] = static_cast<int>(i);
        }
    }

    Part* Get(const EntityId eid)
    {
        const IndexType idx = IndexOf(eid);
        return (InvalidIndex != idx) ? &m_Components[idx] : nullptr;
    }

    const Part* Get(const EntityId eid) const
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

    using Iterator = typename std::vector<Part>::iterator;
    using ConstIterator = typename std::vector<Part>::const_iterator;

    Iterator begin()
    {
        return m_Components.begin();
    }

    Iterator end()
    {
        return m_Components.end();
    }

    ConstIterator begin() const
    {
        return m_Components.begin();
    }

    ConstIterator end() const
    {
        return m_Components.end();
    }

private:

    /// @brief Returns the index of the part with the given entity ID, or -1 if not found.
    int IndexOf(const EntityId id) const
    {
        const auto value = id.Value();

        return value < static_cast<int>(m_Index.size())
        ? m_Index[value]
        : -1;
    }

    /// @brief Returns an iterator to one past the end of the sub-assembly rooted at the given parent.
    std::vector<Part>::iterator SubAssemblyBounds(const EntityId parentId)
    {
        const int parentIdx = IndexOf(parentId);
        if(parentIdx == -1)
        {
            return m_Components.end();
        }

        const auto endIt = m_Components.end();

        auto childIt = m_Components.begin() + parentIdx + 1;

        while(endIt != childIt)
        {
            if(childIt->ParentId != parentId)
            {
                break;
            }

            childIt = SubAssemblyBounds(childIt->Id);
        }

        return childIt;
    }

    std::vector<Part> m_Components;
    std::vector<int> m_Index;
};