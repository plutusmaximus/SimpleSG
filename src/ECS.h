#pragma once

#include "Error.h"

#include <cstdint>
#include <limits>
#include <vector>
#include <string>
#include <format>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <span>

/// @brief An entity identifier.
class EntityId
{
public:

    friend class EcsRegistry;

    using ValueType = uint32_t;

    static constexpr ValueType InvalidValue = std::numeric_limits<ValueType>::max();

    EntityId() = default;

    ValueType Value() const
    {
        return m_Value;
    }

    constexpr bool IsValid() const
    {
        return InvalidValue != m_Value;
    }

    constexpr bool operator<(const EntityId& that) const
    {
        return m_Value < that.m_Value;
    }

    constexpr bool operator==(const EntityId& that) const
    {
        return m_Value == that.m_Value;
    }

private:

    explicit EntityId(const ValueType value)
        : m_Value(value)
    {
    }

    ValueType m_Value{ InvalidValue };
};

namespace std
{
    /// @brief Enable hashing of EntityId for use in unordered containers.
    template<>
    struct hash<EntityId>
    {
        std::size_t operator()(const EntityId eid) const noexcept
        {
            return static_cast<std::size_t>(eid.Value());
        }
    };

    /// @brief Enable formatting of EntityId via std::format.
    template<>
    struct formatter<EntityId>
    {
        constexpr auto parse(std::format_parse_context& ctx)
        {
            return ctx.begin();
        }

        template<typename FormatContext>
        auto format(const EntityId& eid, FormatContext& ctx) const
        {
            return std::format_to(ctx.out(), "{}", eid.Value());
        }
    };
}

/// @brief Interface for a component pool.
class IEcsPool
{
public:
    virtual ~IEcsPool() = default;

    virtual void Remove(const EntityId eid) = 0;
};

/// @brief A pool of components of type C associated with entity IDs.
template<typename C>
class EcsComponentPool : public IEcsPool
{
public:

    using IndexType = EntityId::ValueType;
    static constexpr IndexType InvalidIndex = EntityId::InvalidValue;

    /// @brief Add a component for the given entity ID.
    /// Pass component constructor arguments.
    /// Components are constructed in-place with the given arguments.
    /// A pointer to the newly added component is returned.
    template<typename... Args>
    bool Add(const EntityId eid, Args&&... args)
    {
        if(!everify(!Has(eid) && "Component already exists for entity"))
        {
            return false;
        }

        EnsureIndexes(eid);

        const IndexType idx = static_cast<IndexType>(m_EntityIds.size());
        m_EntityIds.push_back(eid);
        m_Components.emplace_back(std::forward<Args>(args)...);
        m_Index[eid.Value()] = idx;
        return true;
    }

    /// @brief Remove the component for the given entity ID.
    void Remove(const EntityId eid) override
    {
        const IndexType idx = IndexOf(eid);

        if(InvalidIndex == idx)
        {
            return;
        }

        const IndexType last = static_cast<IndexType>(m_EntityIds.size() - 1);

        if (idx != last)
        {
            // move last into removed slot
            const EntityId movedEntity = m_EntityIds[last];
            m_EntityIds[idx] = movedEntity;
            m_Components[idx] = std::move(m_Components[last]);
            m_Index[movedEntity.Value()] = idx;
        }

        m_EntityIds.pop_back();
        m_Components.pop_back();
        m_Index[eid.Value()] = EntityId::InvalidValue;
    }

    /// @brief Get the component for the given entity ID.
    C* Get(const EntityId eid)
    {
        const IndexType idx = IndexOf(eid);
        return (InvalidIndex != idx) ? &m_Components[idx] : nullptr;
    }

    /// @brief Get the component for the given entity ID (const version).
    const C* Get(const EntityId eid) const
    {
        const IndexType idx = IndexOf(eid);
        return (InvalidIndex != idx) ? &m_Components[idx] : nullptr;
    }

    /// @brief Returns true if the given entity ID has an associated component.
    bool Has(const EntityId eid) const
    {
        const auto value = eid.Value();
        return value < m_Index.size() && m_Index[value] != EntityId::InvalidValue;
    }

    /// @brief Get the number of components in the pool.
    size_t size() const
    {
        return m_EntityIds.size();
    }

    using Iterator = typename std::vector<EntityId>::iterator;

    /// @brief Get an iterator to the beginning of the entity IDs.
    Iterator begin()
    {
        return m_EntityIds.begin();
    }

    /// @brief Get an iterator to the end of the entity IDs.
    Iterator end()
    {
        return m_EntityIds.end();
    }

private:

    /// @brief Ensure the index vector is large enough to contain the given entity ID.
    void EnsureIndexes(const EntityId eid)
    {
        if (eid.IsValid() && eid.Value() >= m_Index.size())
        {
            m_Index.resize(eid.Value() + 1, EntityId::InvalidValue);
        }
    }

    IndexType IndexOf(const EntityId eid) const
    {
        return eid.Value() < m_Index.size() ? m_Index[eid.Value()] : InvalidIndex;
    } 

    // Mapping from EntityId to index in the component vector.
    std::vector<IndexType> m_Index;
    // Entity IDs indexed by m_Index.
    std::vector<EntityId> m_EntityIds;
    // Components indexed by m_Index.
    std::vector<C> m_Components;
};

/// @brief The ECS registry that manages entity IDs and their associated components.
class EcsRegistry
{
public:

    /// @brief  Create a new entity ID.
    [[nodiscard]] EntityId Create()
    {
        if (!m_FreeList.empty())
        {
            EntityId eid = m_FreeList.back();
            m_FreeList.pop_back();
            eassert(!IsAlive(eid) && "Entity ID from free list is already alive");
            m_IsAlive[eid.Value()] = true;
            return eid;
        }

        EntityId eid{ m_NextId++ };
        EnsureIndexes(eid);
        m_IsAlive[eid.Value()] = true;
        return eid;
    }

    /// @brief Destroy the given entity ID and remove all associated components.
    void Destroy(const EntityId eid)
    {
        if(!everify(IsAlive(eid) && "Entity is not alive"))
        {
            return;
        }

        //Remove components
        for (auto& [_, poolPtr] : m_Pools)
        {
            poolPtr->Remove(eid);
        }

        m_IsAlive[eid.Value()] = false;

        m_FreeList.push_back(eid);
    }

    /// @brief Add a component of type C for the given entity ID.
    template<typename C, typename... Args>
    bool Add(const EntityId eid, Args&&... args)
    {
        if(!everify(IsAlive(eid) && "Entity is not alive"))
        {
            return false;
        }

        return Pool<C>().Add(eid, std::forward<Args>(args)...);
    }

    /// @brief Remove the component of type C for the given entity ID.
    template<typename C>
    void Remove(const EntityId eid)
    {
        if(!everify(IsAlive(eid) && "Entity is not alive"))
        {
            return;
        }

        if(auto pool = TryGetPool<C>())
        {
            pool->Remove(eid);
        }
    }

    template<typename... Cs>
    Result<std::tuple<Cs&...>> Get(const EntityId eid)
    {
        if(!everify(IsAlive(eid)))
        {
            return std::unexpected(Error("Entity {} is not alive", eid));
        }

        std::tuple<EcsComponentPool<Cs>*...> pools;
        if(!EnsureEntityHasComponents(eid, pools))
        {
            return std::unexpected(Error("Entity {} does not have all requested components", eid));
        }

        // Return references to the components. References become invalid if pools mutate.
        return std::tuple<Cs&...>{ *std::get<EcsComponentPool<Cs>*>(pools)->Get(eid)... };
    }

    template<typename... Cs>
    Result<std::tuple<const Cs&...>> Get(const EntityId eid) const
    {
        if(!everify(IsAlive(eid)))
        {
            return std::unexpected(Error("Entity {} is not alive", eid));
        }

        std::tuple<const EcsComponentPool<Cs>*...> pools;
        if(!EnsureEntityHasComponents(eid, pools))
        {
            return std::unexpected(Error("Entity {} does not have all requested components", eid));
        }

        // Return const references to the components. References become invalid if pools mutate.
        return std::tuple<const Cs&...>{ *std::get<const EcsComponentPool<Cs>*>(pools)->Get(eid)... };
    }

    /// @brief Returns true if the given entity ID has a component of type C.
    template<typename C>
    bool Has(const EntityId eid) const
    {
        const auto pool = TryGetPool<C>();
        return pool && IsAlive(eid) && pool->Has(eid);
    }

    /// @brief Returns true if the given entity ID is alive.
    bool IsAlive(const EntityId eid) const
    {
        return eid.IsValid() && eid.Value() < m_IsAlive.size() && m_IsAlive[eid.Value()];
    }

    /// Forward declaration of View
    template<typename... Cs> class View;

    /// @brief Get a view over all entities that have the given component types.
    /// Range-based for loop can be used to iterate over the resulting View.
    template<typename... Cs>
    View<Cs...> GetView()
    {
        auto pools = std::make_tuple(TryGetPool<Cs>()...);

        return View<Cs...>(pools);
    }

    /// @brief A view over all entities that have the given component types.
    /// Used in conjunction with EcsRegistry::GetView().
    template<typename... Cs>
    class View
    {
        static_assert(sizeof...(Cs) > 0, "GetView requires at least one component type");
    public:

        class Iterator;

        View(std::tuple<EcsComponentPool<Cs>*...> pools)
            : m_Pools(pools)
            , m_It(EmptyList().begin())
            , m_EndIt(EmptyList().end())
        {
            const bool haveAllPools = ((std::get<EcsComponentPool<Cs>*>(m_Pools) != nullptr) &&...);

            if(haveAllPools)
            {
                Init();
            }
        }

        Iterator begin()
        {
            return Iterator(m_Pools, m_It, m_EndIt);
        }

        Iterator end()
        {
            return Iterator(m_Pools, m_EndIt, m_EndIt);
        }

        using EntityIterator = typename std::vector<EntityId>::iterator;

        class Iterator
        {
        public:

            Iterator(
                const std::tuple<EcsComponentPool<Cs>*...>& pools,
                EntityIterator it,
                EntityIterator endIt)
                : m_Pools(pools)
                , m_It(it)
                , m_EndIt(endIt)
            {
                Advance();
            }

            Iterator& operator++()
            {
                eassert(m_It != m_EndIt);
                ++m_It;
                Advance();
                return *this;
            }

            bool operator==(const Iterator& other) const
            {
                return m_It == other.m_It;
            }

            bool operator!=(const Iterator& other) const
            {
                return m_It != other.m_It;
            }

            std::tuple<EntityId, Cs&...> operator*()
            {
                eassert(m_It != m_EndIt);
                const EntityId eid = *m_It;
                return std::tuple<EntityId, Cs&...>{ eid, *std::get<EcsComponentPool<Cs>*>(m_Pools)->Get(eid)... };
            }

        private:

            template<typename C>
            bool HasComponent() const
            {
                return std::get<EcsComponentPool<C>*>(m_Pools)->Has(*m_It);
            }

            bool HasAllComponents() const
            {
                return (HasComponent<Cs>() && ...);
            }

            void Advance()
            {
                while (m_It != m_EndIt)
                {
                    if(HasAllComponents())
                    {
                        break;
                    }
                    ++m_It;
                }
            }

            const std::tuple<EcsComponentPool<Cs>*...>& m_Pools;
            EntityIterator m_It;
            const EntityIterator m_EndIt;
        };

    private:
    
        /// @brief Returns a reference to an empty entity ID list.
        /// Used when there are no component pools.
        static std::vector<EntityId>& EmptyList()
        {
            static std::vector<EntityId> s_empty;
            return s_empty;
        }

        void Init()
        {
            // Select the smallest pool to iterate over.
            std::size_t minSize = std::numeric_limits<std::size_t>::max();
            (SelectSmallestPool<Cs>(minSize),...);
        }

        template<typename C>
        void SelectSmallestPool(size_t& minSize)
        {
            auto pool = std::get<EcsComponentPool<C>*>(m_Pools);

            if(const size_t poolSize = pool->size(); poolSize < minSize)
            {
                minSize = poolSize;
                m_It = pool->begin();
                m_EndIt = pool->end();
            }
        }

        const std::tuple<EcsComponentPool<Cs>*...> m_Pools;
        EntityIterator m_It;
        EntityIterator m_EndIt;
    };

private:

    // Helper to populate pools tuple and verify presence of all component types (non-const version).
    template<typename... Cs>
    bool EnsureEntityHasComponents(const EntityId eid, std::tuple<EcsComponentPool<Cs>*...>& pools)
    {
        pools = std::make_tuple(TryGetPoolForEntity<Cs>(eid)...);
        return (std::get<EcsComponentPool<Cs>*>(pools) && ...);
    }

    // Helper to populate pools tuple and verify presence of all component types (const version).
    template<typename... Cs>
    bool EnsureEntityHasComponents(const EntityId eid, std::tuple<const EcsComponentPool<Cs>*...>& pools) const
    {
        pools = std::make_tuple(TryGetPoolForEntity<Cs>(eid)...);
        return (std::get<const EcsComponentPool<Cs>*>(pools) && ...);
    }

    template<typename C>
    EcsComponentPool<C>* TryGetPool()
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        return (it != m_Pools.end()) ? static_cast<EcsComponentPool<C>*>(it->second.get()) : nullptr;
    }

    template<typename C>
    const EcsComponentPool<C>* TryGetPool() const
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        return (it != m_Pools.end()) ? static_cast<const EcsComponentPool<C>*>(it->second.get()) : nullptr;
    }

    template<typename C>
    EcsComponentPool<C>* TryGetPoolForEntity(const EntityId eid)
    {
        auto pool = TryGetPool<C>();
        return (pool && pool->Has(eid)) ? pool : nullptr;
    }

    template<typename C>
    const EcsComponentPool<C>* TryGetPoolForEntity(const EntityId eid) const
    {
        auto pool = TryGetPool<C>();
        return (pool && pool->Has(eid)) ? pool : nullptr;
    }

    template<typename C>
    EcsComponentPool<C>& Pool()
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        if (it == m_Pools.end())
        {
            auto pool = new EcsComponentPool<C>();
            m_Pools.emplace(tid, pool);
            return *pool;
        }
        return *static_cast<EcsComponentPool<C>*>(it->second.get());
    }

    /// @brief Ensure the alive vector is large enough to contain the given entity ID.
    void EnsureIndexes(const EntityId eid)
    {
        if (eid.IsValid() && eid.Value() >= m_IsAlive.size())
        {
            m_IsAlive.resize(eid.Value() + 1, false);
        }
    }

    std::vector<EntityId> m_FreeList;

    // Using uint8_t vector instead of bool vector to avoid potential issues with bool specialization.
    std::vector<uint8_t> m_IsAlive;

    std::unordered_map<std::type_index, std::unique_ptr<IEcsPool>> m_Pools;

    EntityId::ValueType m_NextId{ 0 };
};
