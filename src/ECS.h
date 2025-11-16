#pragma once

#include "Error.h"

#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <optional>

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
        return std::format_to(ctx.out(), "{}", eid.Value());
    }
};

/// @brief Enable hashing of EntityId for use in unordered containers.
template<>
struct std::hash<EntityId>
{
    std::size_t operator()(const EntityId eid) const noexcept
    {
        return eid.Value();
    }
};

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

    /// @brief Add a component for the given entity ID.
    /// Pass component constructor arguments.
    /// Components are constructed in-place with the given arguments.
    /// The consructed component is returned wrapped in a EcsComponent<C>.
    template<typename... Args>
    C* Add(const EntityId eid, Args&&... args)
    {
        if(!everify(!Has(eid) && "Component already exists for entity"))
        {
            return nullptr;
        }

        EnsureIndexes(eid);

        const IndexType idx = static_cast<IndexType>(m_EntityIds.size());
        m_EntityIds.push_back(eid);
        m_Components.emplace_back(std::forward<Args>(args)...);
        m_Index[eid.Value()] = idx;
        return &m_Components.back();
    }

    /// @brief Get the component for the given entity ID.
    C* Get(const EntityId eid)
    {
        if (!Has(eid))
        {
            return nullptr;
        }

        const IndexType idx = m_Index[eid.Value()];
        return &m_Components[idx];
    }

    /// @brief Get the component for the given entity ID (const version).
    const C* Get(const EntityId eid) const
    {
        if (!Has(eid))
        {
            return nullptr;
        }

        const IndexType idx = m_Index[eid.Value()];
        return &m_Components[idx];
    }

    /// @brief Remove the component for the given entity ID.
    void Remove(const EntityId eid) override
    {
        if (!Has(eid)) return;

        const IndexType idx = m_Index[eid.Value()];
        const IndexType last = static_cast<IndexType>(m_EntityIds.size() - 1);

        if (idx != last)
        {
            //FIXME(KB) - instead of moving set to unused.

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

    /// @brief Returns true if the given entity ID has an associated component.
    bool Has(const EntityId eid) const
    {
        return eid.Value() < m_Index.size() && m_Index[eid.Value()] != EntityId::InvalidValue;
    }

    size_t size() const
    {
        return m_EntityIds.size();
    }

    using Iterator = typename std::vector<EntityId>::iterator;

    Iterator begin()
    {
        return m_EntityIds.begin();
    }
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

    // Mapping from EntityId to index in the component vector.
    std::vector<IndexType> m_Index;
    // Entity IDs indexed by m_Index.
    std::vector<EntityId> m_EntityIds;
    // Components indexed by m_Index.
    std::vector<C> m_Components;
};

/// @brief forward declaration of EcsView
template<typename... Cs> class EcsView;

/// @brief The ECS registry that manages entity IDs and their associated components.
class EcsRegistry
{
public:

    /// @brief  Create a new entity ID.
    EntityId Create()
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

        m_IsAlive[eid.Value()] = false;;

        m_FreeList.push_back(eid);
    }

    /// @brief Add a component of type C for the given entity ID.
    template<typename C, typename... Args>
    C* Add(const EntityId eid, Args&&... args)
    {
        if(!everify(IsAlive(eid) && "Entity is not alive"))
        {
            return nullptr;
        }

        return Pool<C>().Add(eid, std::forward<Args>(args)...);
    }

    /// @brief Get the component of type C for the given entity ID.
    template<typename C>
    C* Get(const EntityId eid)
    {
        if(!everify(IsAlive(eid) && "Entity is not alive"))
        {
            return nullptr;
        }

        return Pool<C>().Get(eid);
    }

    /// @brief Get the component of type C for the given entity ID (const version).
    template<typename C>
    const C* Get(const EntityId eid) const
    {
        if(!everify(IsAlive(eid) && "Entity is not alive"))
        {
            return nullptr;
        }

        return Pool<C>().Get(eid);
    }

    /// @brief Get a view over multiple components for the given entity ID.
    template<typename... Cs>
    Result<EcsView<Cs...>> GetView(const EntityId eid)
    {
        if(!everify(IsAlive(eid)))
        {
            return std::unexpected(Error("Entity {} is not alive", eid));
        }
        
        if((!Has<Cs>(eid) || ...))
        {
            auto missingComponents = (std::string{} + ... + (Has<Cs>(eid) ? "" : (" " + std::string{typeid(Cs).name()})));   
            return std::unexpected(Error("Entity {} does not have all requested components: {}", eid, missingComponents));
        }

        return EcsView<Cs...>(eid, *this);
    }

    /// @brief Returns true if the given entity ID has a component of type C.
    template<typename C>
    bool Has(const EntityId eid) const
    {
        return HavePool<C>() && Pool<C>().Has(eid);
    }

    /// @brief Returns true if the given entity ID is alive.
    bool IsAlive(const EntityId eid) const
    {
        return eid.IsValid() && eid.Value() < m_IsAlive.size() && m_IsAlive[eid.Value()];
    }

    /// Forward declaration of FilteredView
    template<typename... Cs> class FilteredView;

    /// @brief Get a filtered view over all entities that have the given component types.
    /// Range-based for loop can be used to iterate over the resulting FilteredView.
    template<typename... Cs>
    FilteredView<Cs...> Filter()
    {
        return FilteredView<Cs...>(*this);
    }

    /// @brief A filtered view over all entities that have the given component types.
    /// Used in conjunction with EcsRegistry::Filter().
    template<typename... Cs>
    class FilteredView
    {
    public:

        class Iterator;

        FilteredView(EcsRegistry& reg)
            : m_Reg(reg)
        {
            Init();
        }

        Iterator begin()
        {
            return Iterator(m_Reg, m_It, m_EndIt);
        }

        Iterator end()
        {
            return Iterator(m_Reg, m_EndIt, m_EndIt);
        }

        using EntityIterator = typename std::vector<EntityId>::iterator;

        class Iterator
        {
        public:

            Iterator(
                EcsRegistry& reg,
                EntityIterator it,
                EntityIterator endIt)
                : m_Reg(reg)
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

            bool operator!=(const Iterator& other) const
            {
                return m_It != other.m_It;
            }

            EcsView<Cs...> operator*()
            {
                eassert(m_It != m_EndIt);
                return EcsView<Cs...>(*m_It, m_Reg);
            }

        private:

            void Advance()
            {
                while (m_It != m_EndIt)
                {
                    const EntityId eid = *m_It;
                    if ((m_Reg.Has<Cs>(eid) && ...))
                    {
                        break;
                    }
                    ++m_It;
                }
            }

            EcsRegistry& m_Reg;

            EntityIterator m_It;
            const EntityIterator m_EndIt;
        };

    private:

        void Init()
        {
            bool missingOrEmpty = false;
            (CheckMissingOrEmpty<Cs>(missingOrEmpty), ...);
            if (missingOrEmpty)
            {
                m_It = {};
                m_EndIt = {};
                return;
            }

            std::size_t minSize = std::numeric_limits<std::size_t>::max();
            (SelectSmallestPool<Cs>(minSize),...);
        }

        template<typename C>
        void CheckMissingOrEmpty(bool& missingOrEmpty)
        {
            if (missingOrEmpty)
                return;

            if (!m_Reg.template HavePool<C>())
            {
                missingOrEmpty = true;
                return;
            }

            auto& pool = m_Reg.template Pool<C>();
            if (pool.size() == 0)
            {
                missingOrEmpty = true;
            }
        }

        template<typename C>
        void SelectSmallestPool(size_t& minSize)
        {
            if(const size_t poolSize = m_Reg.Pool<C>().size(); poolSize < minSize)
            {
                minSize = poolSize;
                m_It = m_Reg.Pool<C>().begin();
                m_EndIt = m_Reg.Pool<C>().end();
            }
        }
        EcsRegistry& m_Reg;

        EntityIterator m_It;
        EntityIterator m_EndIt;
    };

private:

    template<typename C>
    bool HavePool() const
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        return it != m_Pools.end();
    }

    template<typename C>
    EcsComponentPool<C>& Pool()
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        if (it == m_Pools.end())
        {
            auto ptr = std::make_unique<EcsComponentPool<C>>();
            auto* raw = ptr.get();
            m_Pools.emplace(tid, std::move(ptr));
            return *raw;
        }
        return *static_cast<EcsComponentPool<C>*>(it->second.get());
    }

    template<typename C>
    const EcsComponentPool<C>& Pool() const
    {
        auto tid = std::type_index(typeid(C));
        auto it = m_Pools.find(tid);
        eassert(it != m_Pools.end() && "Pool does not exist");
        return *static_cast<const EcsComponentPool<C>*>(it->second.get());
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
    std::vector<bool> m_IsAlive;
    std::unordered_map<std::type_index, std::unique_ptr<IEcsPool>> m_Pools;

    EntityId::ValueType m_NextId{ 0 };

};

/// @brief A view over multiple components for a given entity.
template<typename... Cs>
class EcsView
{
public:

    EcsView(const EntityId eid, EcsRegistry& reg)
        : Eid(eid)
        , m_Reg(reg)
    {
    }

    /// @brief Enable structured bindings.
    template<std::size_t I>
    auto* get()
    {
        using T = std::tuple_element_t<I, std::tuple<Cs...>>;
        return m_Reg.Get<T>(Eid);
    }

    /// @brief Enable structured bindings.
    template<std::size_t I>
    const auto* get() const
    {
        using T = std::tuple_element_t<I, std::tuple<Cs...>>;
        return m_Reg.Get<T>(Eid);
    }

    template<typename T>
    T* get()
    {
        static_assert((std::is_same_v<T, Cs> || ...), "T must be one of Cs...");
        return m_Reg.Get<T>(Eid);
    }

    const EntityId Eid;

private:

    EcsRegistry& m_Reg;
};

/// @brief Enable structured bindings for View.
namespace std
{
    template<typename... Cs>
    struct tuple_size<EcsView<Cs...>> : std::integral_constant<size_t, sizeof...(Cs)> {};

    template<size_t I, typename... Cs>
    struct tuple_element<I, EcsView<Cs...>>
    {
        using type = std::tuple_element_t<I, std::tuple<std::add_pointer_t<Cs>...>>;
    };
}