#pragma once

#include "Error.h"

#include <cstdint>
#include <limits>
#include <vector>
#include <format>
#include <tuple>
#include <typeindex>
#include <unordered_map>

/// @brief An entity identifier.
class EntityId
{
public:

    friend class EcsRegistry;

    using ValueType = uint32_t;
    using GenerationType = uint32_t;

    static constexpr ValueType InvalidValue = std::numeric_limits<ValueType>::max();
    static constexpr GenerationType InvalidGeneration = std::numeric_limits<GenerationType>::max();

    EntityId() = default;

    ValueType Value() const
    {
        return m_Value;
    }

    GenerationType Generation() const
    {
        return m_Generation;
    }

    constexpr bool IsValid() const
    {
        return InvalidValue != m_Value;
    }

    constexpr bool operator<(const EntityId& that) const
    {
        return m_Value < that.m_Value || (m_Value == that.m_Value && m_Generation < that.m_Generation);
    }

    constexpr bool operator==(const EntityId& that) const
    {
        return m_Value == that.m_Value && m_Generation == that.m_Generation;
    }

private:

    explicit EntityId(const ValueType value, const GenerationType generation)
        : m_Value(value)
        , m_Generation(generation)
    {
    }

    ValueType m_Value{ InvalidValue };
    GenerationType m_Generation{ InvalidGeneration };
};

/// @brief Enable hashing of EntityId for use in unordered containers.
template<>
struct std::hash<EntityId>
{
    std::size_t operator()(const EntityId eid) const noexcept
    {
        return static_cast<std::size_t>(eid.Value());
    }
};

/// @brief Enable formatting of EntityId via std::format.
template<>
struct std::formatter<EntityId>
{
    constexpr auto parse(std::format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const EntityId& eid, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "{}:{}", eid.Value(), eid.Generation());
    }
};

/// @brief Interface for a component pool.
class IEcsPool
{
public:
    using IndexType = EntityId::ValueType;
    static constexpr IndexType InvalidIndex = EntityId::InvalidValue;

    virtual ~IEcsPool() = default;

    virtual void Remove(const EntityId eid) = 0;
};

/// @brief A pool of components of type C associated with entity IDs.
template<typename C>
class EcsComponentPool : public IEcsPool
{
public:

    /// @brief Reserve space for components and entity IDs.
    void Reserve(const size_t entityCount)
    {
        m_EntityIds.reserve(entityCount);
        m_Components.reserve(entityCount);
        m_Index.reserve(entityCount);
    }

    /// @brief Add a component for the given entity ID.
    /// Returns true if component was added.
    bool Add(const EntityId eid, const C& component)
    {
        if(!everify(!Has(eid), "Component already exists for entity"))
        {
            return false;
        }

        EnsureIndexes(eid);

        const IndexType idx = static_cast<IndexType>(m_EntityIds.size());
        m_EntityIds.push_back(eid);
        m_Components.emplace_back(component);
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

    std::tuple<EntityId, C&> operator[](IndexType index)
    {
        eassert(index < size(), "Index out of bounds");
        return { m_EntityIds[index], m_Components[index] };
    }

    std::tuple<EntityId, const C&> operator[](IndexType index) const
    {
        eassert(index < size(), "Index out of bounds");
        return { m_EntityIds[index], m_Components[index] };
    }

    C& operator[](const EntityId eid)
    {
        const IndexType index = IndexOf(eid);
        eassert(index != InvalidIndex, "EntityId not found");
        return m_Components[index];
    }

    const C& operator[](const EntityId eid) const
    {
        const IndexType index = IndexOf(eid);
        eassert(index != InvalidIndex, "EntityId not found");
        return m_Components[index];
    }

    C* TryGet(const EntityId eid)
    {
        const IndexType index = IndexOf(eid);
        if (index != InvalidIndex)
        {
            return &m_Components[index];
        }
        return nullptr;
    }

    const C* TryGet(const EntityId eid) const
    {
        const IndexType index = IndexOf(eid);
        if (index != InvalidIndex)
        {
            return &m_Components[index];
        }
        return nullptr;
    }

    /// @brief Returns true if the given entity ID has an associated component.
    bool Has(const EntityId eid) const
    {
        return IndexOf(eid) != InvalidIndex;
    }

    /// @brief Get the index of the component for the given entity ID, or InvalidIndex if not found.
    /// Adding and/or removing components may invalidate indices.
    IndexType IndexOf(const EntityId eid) const
    {
        const auto value = eid.Value();
        if(value < m_Index.size())
        {
            const auto index = m_Index[value];
            if(index != InvalidIndex && m_EntityIds[index] == eid)
            {
                return index;
            }
        }

        return InvalidIndex;
    }

    /// @brief Get the number of components in the pool.
    size_t size() const
    {
        return m_EntityIds.size();
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

    // Mapping from EntityId to index in the component vector.
    // This allows for quick lookup of components by entity ID.
    // It's a sparse index, so there will be gaps.
    // It's length will be at least the maximum entity ID value.
    // This is usually ok because entity IDs are recycled.
    // The index will stabilize at some point - when the number of
    // entities created and retired balance.
    std::vector<IndexType> m_Index;
    // Entity IDs indexed by m_Index.
    std::vector<EntityId> m_EntityIds;
    // Components indexed by m_Index.
    std::vector<C> m_Components;
};

/// @brief A view over all entities that have the given component types.
template<typename... Cs>
class EcsView
{
    static_assert(sizeof...(Cs) > 0, "GetView requires at least one component type");
public:

    using IndexType = IEcsPool::IndexType;

    class Iterator
    {
    public:

        explicit Iterator(const std::tuple<EcsComponentPool<Cs>*...>& pools, const IndexType idx, const size_t size)
            : m_Pools(pools)
            , m_Idx(idx)
            , m_Size(size)
        {
            Advance();
        }

        Iterator& operator++()
        {
            if(everify(m_Idx < m_Size, "Index out of bounds"))
            {
                ++m_Idx;
                Advance();
            }
            return *this;
        }

        bool operator==(const Iterator& other) const
        {
            return m_Idx == other.m_Idx;
        }

        bool operator!=(const Iterator& other) const
        {
            return m_Idx != other.m_Idx;
        }

        std::tuple<EntityId, Cs&...> operator*()
        {
            eassert(m_Idx < m_Size, "Index out of bounds");
            return MakeRefTuple(m_Current);
        }

    private:

        template<typename C1, typename... CRest>
        std::tuple<CRest*...> TryGet(const EntityId eid) const
        {
            return std::tuple<CRest*...>{ std::get<EcsComponentPool<CRest>*>(m_Pools)->TryGet(eid)... };
        }

        template<Cs...>
        std::tuple<EntityId, Cs&...> MakeRefTuple(std::tuple<EntityId, Cs*...> ptrs)
        {
            return std::apply([](EntityId eid, Cs*... ptrs)
            {
                return std::tuple<EntityId, Cs&...>{ eid, *ptrs... };
            }, ptrs);
        }

        /// @brief Advance the iterator to the next entity that has all components.
        void Advance()
        {
            while (m_Idx < m_Size)
            {
                // Component from first pool
                auto current = std::get<0>(m_Pools)->operator[](m_Idx);
                // EntityId of current component
                const auto eid = std::get<0>(current);
                // Try to get components from other pools
                auto tryComp = TryGet<Cs...>(eid);

                // Check if all components were found
                bool hasAll = std::apply([](auto*... ptrs) { return (... && ptrs); }, tryComp);

                if(hasAll)
                {
                    // All components found, build current tuple
                    m_Current = std::tuple_cat(
                        std::tuple{ eid, &std::get<1>(current) },
                        tryComp);
                    break;
                }

                ++m_Idx;
            }
        }

        // Pools for each component type
        const std::tuple<EcsComponentPool<Cs>*...>& m_Pools;
        // Current index in iteration
        IndexType m_Idx;
        // Total number of elements in the first pool
        const size_t m_Size;
        // Current tuple of EntityId and component pointers
        std::tuple<EntityId, Cs*...> m_Current;
    };

    explicit EcsView(std::tuple<EcsComponentPool<Cs>*...> pools)
        : m_Pools(pools)
        , m_HaveAllPools(((std::get<EcsComponentPool<Cs>*>(m_Pools) != nullptr) &&...))
    {
    }

    Iterator begin()
    {
        return m_HaveAllPools
            ? Iterator(m_Pools, 0, std::get<0>(m_Pools)->size())
            : Iterator(m_Pools, 0, 0);
    }

    Iterator end()
    {
        return m_HaveAllPools
            ? Iterator(m_Pools, static_cast<IndexType>(std::get<0>(m_Pools)->size()), std::get<0>(m_Pools)->size())
            : Iterator(m_Pools, 0, 0);
    }

private:

    const std::tuple<EcsComponentPool<Cs>*...> m_Pools;
    const bool m_HaveAllPools;
};

/// @brief The ECS registry that manages entity IDs and their associated components.
class EcsRegistry
{
public:

    EcsRegistry() = default;

    EcsRegistry(const EcsRegistry&) = delete;
    EcsRegistry& operator=(const EcsRegistry&) = delete;
    EcsRegistry(EcsRegistry&&) = delete;
    EcsRegistry& operator=(EcsRegistry&&) = delete;

    /// @brief  Create a new entity ID.
    [[nodiscard]] EntityId Create()
    {
        if (!m_FreeList.empty())
        {
            EntityId eid = m_FreeList.back();
            m_FreeList.pop_back();
            eassert(!IsAlive(eid), "Entity ID from free list is already alive");
            m_IsAlive[eid.Value()] = true;
            return EntityId{ eid.Value(), ++eid.m_Generation };
        }

        const EntityId eid{ m_NextId++, 0 };
        EnsureIndexes(eid);
        m_IsAlive[eid.Value()] = true;
        return eid;
    }

    /// @brief Destroy the given entity ID and remove all associated components.
    void Destroy(const EntityId eid)
    {
        if(!everify(IsAlive(eid), "Entity is not alive"))
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

    /// @brief Reserve space for components and internal data structures.
    template<typename... Cs>
    void Reserve(const size_t entityCount)
    {
        (Pool<Cs>().Reserve(entityCount), ...);
        m_IsAlive.reserve(entityCount);
        m_FreeList.reserve(entityCount);
    }

    /// @brief Add components of types Cs for the given entity ID.
    template<typename... Cs>
    bool Add(const EntityId eid, const Cs&... components)
    {
        if(!everify(IsAlive(eid), "Entity is not alive"))
        {
            return false;
        }

        return (Pool<Cs>().Add(eid, components) && ...);
    }

    /// @brief Remove the component of type C for the given entity ID.
    template<typename C>
    void Remove(const EntityId eid)
    {
        if(!everify(IsAlive(eid), "Entity is not alive"))
        {
            return;
        }

        if(auto pool = TryGetPool<C>())
        {
            pool->Remove(eid);
        }
    }

    /// @brief Get a tuple of references to the components of types Cs for the given entity ID.
    template<typename... Cs>
    requires (sizeof...(Cs) >= 2)
    std::tuple<Cs&...> Get(const EntityId eid)
    {
        eassert(IsAlive(eid), "Entity is not alive");

        auto pools = std::make_tuple(TryGetPoolForEntity<Cs>(eid)...);

        eassert((std::get<EcsComponentPool<Cs>*>(pools) && ...), "Entity does not have all requested components");

        return
            std::tuple<Cs&...>{ std::get<EcsComponentPool<Cs>*>(pools)->operator[](eid)... };
    }

    /// @brief Get a reference to the component of type C for the given entity ID.
    template<typename C>
    C& Get(const EntityId eid)
    {
        eassert(IsAlive(eid), "Entity is not alive");

        auto pool = TryGetPoolForEntity<C>(eid);

        eassert(pool, "Entity does not have requested component");

        return pool->operator[](eid);
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

    /// @brief Get a view over all entities that have the given component types.
    /// Range-based for loop can be used to iterate over the resulting View.
    /// References to components become invalid if pools mutate.
    template<typename... Cs>
    EcsView<Cs...> GetView()
    {
        auto pools = std::make_tuple(TryGetPool<Cs>()...);

        return EcsView<Cs...>(pools);
    }

    /// @brief Clear all entities and components from the registry.
    void Clear()
    {
        m_Pools.clear();
        m_IsAlive.clear();
        m_FreeList.clear();
        m_NextId = 0;
    }

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

    /// @brief Attempt to get the component pool for type C.
    template<typename C>
    EcsComponentPool<C>* TryGetPool()
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        return (it != m_Pools.end()) ? static_cast<EcsComponentPool<C>*>(it->second.get()) : nullptr;
    }

    /// @brief Attempt to get the component pool for type C.
    template<typename C>
    const EcsComponentPool<C>* TryGetPool() const
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        return (it != m_Pools.end()) ? static_cast<const EcsComponentPool<C>*>(it->second.get()) : nullptr;
    }

    /// @brief Attempt to get the component pool for type C that contains the given entity ID.
    template<typename C>
    EcsComponentPool<C>* TryGetPoolForEntity(const EntityId eid)
    {
        auto pool = TryGetPool<C>();
        return (pool && pool->Has(eid)) ? pool : nullptr;
    }

    /// @brief Attempt to get the component pool for type C that contains the given entity ID.
    template<typename C>
    const EcsComponentPool<C>* TryGetPoolForEntity(const EntityId eid) const
    {
        auto pool = TryGetPool<C>();
        return (pool && pool->Has(eid)) ? pool : nullptr;
    }

    /// @brief Get the component pool for type C, creating it if it doesn't exist.
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

    /// Collection of recycled entity IDs.
    std::vector<EntityId> m_FreeList;

    // Using uint8_t vector instead of bool vector to avoid potential issues with bool specialization.
    std::vector<uint8_t> m_IsAlive;

    std::unordered_map<std::type_index, std::unique_ptr<IEcsPool>> m_Pools;

    EntityId::ValueType m_NextId{ 0 };
};
