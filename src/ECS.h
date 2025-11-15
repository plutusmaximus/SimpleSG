#pragma once

#include "Error.h"

#include <typeindex>
#include <set>
#include <unordered_map>
#include <optional>

/// @brief An entity identifier.
class EntityId
{
public:

    using ValueType = uint32_t;

    static constexpr ValueType InvalidValue = std::numeric_limits<ValueType>::max();

    EntityId() = default;

    explicit EntityId(const ValueType value)
        : m_Value(value)
    {
    }

    operator ValueType()
    {
        return m_Value;
    }

    operator const ValueType() const
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

private:

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
        return std::format_to(ctx.out(), "{}", EntityId::ValueType(eid));
    }
};

/// @brief Enable hashing of EntityId for use in unordered containers.
template<>
struct std::hash<EntityId>
{
    std::size_t operator()(const EntityId eid) const noexcept
    {
        return static_cast<EntityId::ValueType>(eid);
    }
};

/// @brief Interface for a component pool.
class IEcsPool
{
public:
    virtual ~IEcsPool() = default;

    virtual void Remove(const EntityId eid) = 0;
};

/// @brief A reference wrapper for a component associated with an entity.
/// Check for validity before use by converting to bool or using operator!.
/// Access the underlying component through operator*.
/// @tparam C 
template<typename C>
struct Component
{
public:
    Component() = default;
    Component(C& c) : m_Ref(c) {}

    explicit operator bool() const noexcept
    {
        return m_Ref.has_value();
    }

    C& operator*()
    {
        eassert(static_cast<bool>(*this));
        return m_Ref.value().get();
    }

    const C& operator*() const
    {
        eassert(static_cast<bool>(*this));
        return m_Ref.value().get();
    }

    /// @brief Assign a new value to the underlying component.
    /// Note that the underlying reference must be valid.
    Component& operator=(const C& value)
    {
        if(*this)
        {
            m_Ref.value().get() = value;
        }
        return *this;
    }

    /// @brief Assign to the underlying component from another Component reference.
    /// Note that both Component references must be valid.
    Component& operator=(const Component& other)
    {
        if (other && *this)
        {
            m_Ref.value().get() = *other;
        }
        return *this;
    }

    /// @brief Returns true if both Component references are valid and their underlying components are equal.
    bool operator==(const Component<C>& other) const
    {
        return
            static_cast<bool>(*this)
            && static_cast<bool>(other)
            && (**this == *other);
    }

    /// @brief Returns true if the Component reference is valid and its underlying component
    /// is equal to the given value.
    /// @param lhs 
    /// @param rhs 
    /// @return 
    friend bool operator==(const Component<C>& lhs, const C& rhs)
    {
        return
            static_cast<bool>(lhs)
            && (*lhs == rhs);
    }

    /// @brief Returns true if Component reference is valid and the given value is equal to
    /// the underlying component of the Component reference.
    friend bool operator==(const C& lhs, const Component<C>& rhs)
    {
        return operator==(rhs, lhs);
    }

    bool operator!=(const Component<C>& other) const
    {
        return !(operator==(other));
    }

    friend bool operator!=(const Component<C>& lhs, const C& rhs)
    {
        return !(lhs == rhs);
    }

    friend bool operator!=(const C& lhs, const Component<C>& rhs)
    {
        return !(lhs == rhs);
    }

    /// @brief Rebinding the underlying reference is not allowed.
    Component& operator=(C&) = delete;
    Component& operator=(std::reference_wrapper<C>) = delete;
    Component& operator=(std::optional<std::reference_wrapper<C>>) = delete;
private:

    std::optional<std::reference_wrapper<C>> m_Ref;
};

/// @brief A pool of components of type C associated with entity IDs.
template<typename C>
class EcsComponentPool : public IEcsPool
{
public:

    using IndexType = EntityId::ValueType;

    /// @brief Add a component for the given entity ID.
    template<typename... Args>
    Component<C> Add(const EntityId eid, Args&&... args)
    {
        if (Has(eid))
        {
            return {};
        }

        EnsureIndexes(eid);

        const IndexType idx = static_cast<IndexType>(m_EntityIds.size());
        m_EntityIds.push_back(eid);
        m_Components.emplace_back(std::forward<Args>(args)...);
        m_Index[eid] = idx;
        return m_Components.back();
    }

    /// @brief Get the component for the given entity ID.
    Component<C> Get(const EntityId eid)
    {
        if (!Has(eid))
        {
            return {};
        }

        const IndexType idx = m_Index[eid];
        return m_Components[idx];

    }

    /// @brief Get the component for the given entity ID (const version).
    Component<const C> Get(const EntityId eid) const
    {
        if (!Has(eid))
        {
            return {};
        }

        const IndexType idx = m_Index[eid];
        return m_Components[idx];
    }

    /// @brief Remove the component for the given entity ID.
    void Remove(const EntityId eid) override
    {
        if (!Has(eid)) return;

        const IndexType idx = m_Index[eid];
        const IndexType last = static_cast<IndexType>(m_EntityIds.size() - 1);

        if (idx != last)
        {
            // move last into removed slot
            const EntityId movedEntity = m_EntityIds[last];
            m_EntityIds[idx] = movedEntity;
            m_Components[idx] = std::move(m_Components[last]);
            m_Index[movedEntity] = idx;
        }

        m_EntityIds.pop_back();
        m_Components.pop_back();
        m_Index[eid] = EntityId::InvalidValue;
    }

    /// @brief Returns true if the given entity ID has an associated component.
    bool Has(const EntityId eid) const
    {
        return eid < m_Index.size() && m_Index[eid] != EntityId::InvalidValue;
    }

private:

    /// @brief Ensure the index vector is large enough to contain the given entity ID.
    void EnsureIndexes(const EntityId eid)
    {
        if (eid.IsValid() && eid >= m_Index.size())
        {
            m_Index.resize(eid + 1, EntityId::InvalidValue);
        }
    }

    std::vector<EntityId> m_EntityIds;
    std::vector<C> m_Components;
    std::vector<IndexType> m_Index;
};

/// @brief forward declaration of View
template<typename... Cs> class View;

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
            m_Alive.insert(eid);
            return eid;
        }

        EntityId eid{ m_NextId++ };
        m_Alive.insert(eid);
        return eid;
    }

    /// @brief Destroy the given entity ID and remove all associated components.
    void Destroy(const EntityId eid)
    {
        eassert(IsAlive(eid) && "Entity is not alive");

        //Remove components
        for (auto& [_, poolPtr] : m_Pools)
        {
            poolPtr->Remove(eid);
        }

        m_Alive.erase(eid);

        m_FreeList.push_back(eid);
    }

    /// @brief Add a component of type C for the given entity ID.
    template<typename C, typename... Args>
    Component<C> Add(const EntityId eid, Args&&... args)
    {
        eassert(IsAlive(eid) && "Entity is not alive");

        return Pool<C>().Add(eid, std::forward<Args>(args)...);
    }

    /// @brief Get the component of type C for the given entity ID.
    template<typename C>
    Component<C> Get(const EntityId eid)
    {
        eassert(IsAlive(eid) && "Entity is not alive");

        return Pool<C>().Get(eid);
    }

    /// @brief Get the component of type C for the given entity ID (const version).
    template<typename C>
    Component<const C> Get(const EntityId eid) const
    {
        eassert(IsAlive(eid) && "Entity is not alive");

        return Pool<C>().Get(eid);
    }

    /// @brief Get a view over multiple components for the given entity ID.
    template<typename... Cs>
    Result<View<Cs...>> GetView(const EntityId eid)
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

        return View<Cs...>(eid, *this);
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
        return m_Alive.contains(eid);
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
        }

        Iterator begin()
        {
            return Iterator(m_Reg);
        }

        Iterator end()
        {
            Iterator it(m_Reg);
            it.m_It = m_Reg.m_Alive.end();
            return it;
        }

        class Iterator
        {
        public:

            Iterator(EcsRegistry& reg)
                : m_Reg(reg)
            {
                Advance();
            }

            Iterator& operator++()
            {
                eassert(m_It != m_Reg.m_Alive.end());
                Advance();
                return *this;
            }

            bool operator!=(const Iterator& other) const
            {
                return m_It != other.m_It;
            }

            View<Cs...> operator*()
            {
                eassert(m_It != m_Reg.m_Alive.end());
                return View<Cs...>(*m_It, m_Reg);
            }

        private:
            void Advance()
            {
                while (m_It != m_Reg.m_Alive.end())
                {
                    EntityId eid = *m_It;
                    if ((m_Reg.Has<Cs>(eid) && ...))
                    {
                        break;
                    }
                    ++m_It;
                }
            }

            EcsRegistry& m_Reg;
            decltype(m_Reg.m_Alive.begin()) m_It = m_Reg.m_Alive.begin();
        };

    private:
        EcsRegistry& m_Reg;
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

    std::vector<EntityId> m_FreeList;
    std::set<EntityId> m_Alive;
    std::unordered_map<std::type_index, std::unique_ptr<IEcsPool>> m_Pools;

    EntityId::ValueType m_NextId{ 0 };

};

/// @brief A view over multiple components for a given entity.
template<typename... Cs>
class View
{
public:

    View(const EntityId eid, EcsRegistry& reg)
        : Eid(eid)
        , m_Reg(reg)
    {
    }

    /// @brief Enable structured bindings.
    template<std::size_t I>
    auto& get()
    {
        using T = std::tuple_element_t<I, std::tuple<Cs...>>;
        return *m_Reg.Get<T>(Eid);
    }

    /// @brief Enable structured bindings.
    template<std::size_t I>
    const auto& get() const
    {
        using T = std::tuple_element_t<I, std::tuple<Cs...>>;
        return *m_Reg.Get<T>(Eid);
    }

    template<typename T>
    T& get()
    {
        static_assert((std::is_same_v<T, Cs> || ...), "T must be one of Cs...");
        return *m_Reg.Get<T>(Eid);
    }

    const EntityId Eid;

private:

    EcsRegistry& m_Reg;
};

/// @brief Enable structured bindings for View.
namespace std
{
    template<typename... Cs>
    struct tuple_size<View<Cs...>> : std::integral_constant<size_t, sizeof...(Cs)> {};

    template<size_t I, typename... Cs>
    struct tuple_element<I, View<Cs...>>
    {
        using type = std::tuple_element_t<I, std::tuple<Cs...>>;
    };
}