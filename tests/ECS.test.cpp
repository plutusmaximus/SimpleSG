#include <gtest/gtest.h>

#include <random>
#include <array>
#include <unordered_set>

#include "ECS.h"

namespace
{
    std::random_device RD;
    std::mt19937 Rng(RD());
    std::uniform_int_distribution<> RandomInt(0, 10000);
    std::uniform_real_distribution<float> RandomFloat(0, 10000);
    const std::string CharSet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<> CharDist(0, CharSet.size() - 1);
    std::uniform_int_distribution<> StringLenDist(5, 20);

    /// @brief Generates a random string.
    /// @tparam Generator 
    /// @param rng 
    /// @return 
    template<typename Generator>
    std::string RandomString(Generator& rng)
    {
        const int length = StringLenDist(rng);
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i)
        {
            result += CharSet[CharDist(rng)];
        }
        return result;
    }

    /// @brief A simple component for testing.
    struct ComponentA
    {
        int a;

        bool operator==(const ComponentA&) const = default;
    };

    /// @brief A simple component for testing.
    struct ComponentB
    {
        float x, y, z;

        bool operator==(const ComponentB&) const = default;
    };

    /// @brief A more complex component for testing.
    struct ComponentC
    {
        float t, u, v;
        std::string Value;
        int n;

        bool operator==(const ComponentC&) const = default;
    };

    struct ComponentD
    {
        std::string name;
        double value;
        bool operator==(const ComponentD&) const = default;
    };

    /// @brief Generates a random value for the given type.
    template<typename T>
    T RandomValue();

    template<>
    ComponentA RandomValue<ComponentA>()
    {
        return ComponentA{ RandomInt(Rng) };
    }

    template<>
    ComponentB RandomValue<ComponentB>()
    {
        return ComponentB{ RandomFloat(Rng),RandomFloat(Rng), RandomFloat(Rng) };
    }

    template<>
    ComponentC RandomValue<ComponentC>()
    {
        return ComponentC
        {
            RandomFloat(Rng),RandomFloat(Rng), RandomFloat(Rng),
            RandomString(Rng),
            RandomInt(Rng)
        };
    }

    template<>
    ComponentD RandomValue<ComponentD>()
    {
        return ComponentD{
            RandomString(Rng),
            RandomFloat(Rng)
        };
    }

    constexpr int MAX_ENTITIES = 1000;

    /// @brief Use the registry to create a number of entity IDs.
    std::vector<EntityId> CreateEntityIds(EcsRegistry& reg, size_t count)
    {
        std::vector<EntityId> eids(count);
        for (auto& eid : eids)
        {
            eid = reg.Create();
        }

        return eids;
    }

    /// @brief Use the registry to create the maximum number of entity IDs.
    std::vector<EntityId> CreateEntityIds(EcsRegistry& reg)
    {
        return CreateEntityIds(reg, MAX_ENTITIES);
    }

    // ==================== EntityId Tests ====================

    /// @brief Confirm a default constructed EntityId contains an invalid value.
    TEST(EntityId, DefaultConstruct_InvalidValue)
    {
        EntityId eid;

        EXPECT_FALSE(eid.IsValid());
        EXPECT_EQ(eid.Value(), EntityId::InvalidValue);
    }

    /// @brief Confirm an EntityId created via registry is valid.
    TEST(EntityId, CreateViaRegistry_ValidValue)
    {
        EcsRegistry reg;
        
        const auto eid1 = reg.Create();

        EXPECT_TRUE(eid1.IsValid());
        EXPECT_NE(eid1.Value(), EntityId::InvalidValue);

        // Test copy constructor
        EntityId eid2 = eid1;
        EXPECT_TRUE(eid2.IsValid());
        EXPECT_EQ(eid1, eid2);
        EXPECT_EQ(eid2.Value(), eid1.Value());

        // Test assignment operator
        EntityId eid3;
        eid3 = eid1;
        EXPECT_TRUE(eid3.IsValid());
        EXPECT_EQ(eid1, eid3);
        EXPECT_EQ(eid3.Value(), eid1.Value());
    }

    /// @brief Test EntityId less-than operator for sorting.
    TEST(EntityId, LessThanOperator_OrderingConsistency_SortsCorrectly)
    {
        EcsRegistry reg;

        const auto eid1 = reg.Create();
        const auto eid2 = reg.Create();
        const auto eid3 = reg.Create();

        EXPECT_TRUE(eid1 < eid2);
        EXPECT_TRUE(eid1 < eid3);
        EXPECT_TRUE(eid2 < eid3);
        EXPECT_FALSE(eid3 < eid2);
        EXPECT_FALSE(eid2 < eid1);
        EXPECT_FALSE(eid1 < eid1);

        // Test in container
        std::vector<EntityId> eids = { eid3, eid2, eid1, eid3 };
        std::sort(eids.begin(), eids.end());
        EXPECT_EQ(eids[0], eid1);
        EXPECT_EQ(eids[1], eid2);
        EXPECT_EQ(eids[2], eid3);
        EXPECT_EQ(eids[3], eid3);
    }

    /// @brief Test EntityId equality operator.
    TEST(EntityId, EqualityOperator_SameValue_ReturnsTrue)
    {
        EcsRegistry reg;
        const auto eid1 = reg.Create();
        const auto eid2 = eid1;
        const auto eid3 = reg.Create();

        EXPECT_TRUE(eid1 == eid2);
        EXPECT_FALSE(eid1 == eid3);
        EXPECT_TRUE(eid1 == eid1);
    }

    /// @brief Test EntityId formatting with std::format.
    TEST(EntityId, Format_ProducesCorrectString)
    {
        EcsRegistry reg;
        const auto eid = reg.Create();
        std::string formatted = std::format("{}", eid);
        EXPECT_EQ(formatted, std::to_string(eid.Value()));

        EntityId invalidEid;
        std::string formattedInvalid = std::format("{}", invalidEid);
        EXPECT_EQ(formattedInvalid, std::to_string(EntityId::InvalidValue));
    }

    /// @brief Test EntityId hashing for use in unordered containers.
    TEST(EntityId, Hash_UnorderedMap_WorksCorrectly)
    {
        EcsRegistry reg;

        const auto eid1 = reg.Create();
        const auto eid2 = reg.Create();
        const EntityId eid3 = eid1;

        std::unordered_map<EntityId, int> map;

        map[eid1] = 1;
        map[eid2] = 2;
        map[eid3] = 10;  // should overwrite eid1's value

        EXPECT_EQ(map.size(), 2);
        EXPECT_EQ(map[eid1], 10);
        EXPECT_EQ(map[eid2], 2);
    }

    /// @brief Test EntityId hashing in unordered_set.
    TEST(EntityId, Hash_UnorderedSet_WorksCorrectly)
    {
        EcsRegistry reg;

        const auto eid1 = reg.Create();
        const auto eid2 = reg.Create();
        const EntityId eid3 = eid1;

        std::unordered_set<EntityId> set;

        set.insert(eid1);
        set.insert(eid2);
        set.insert(eid3);

        EXPECT_EQ(set.size(), 2);
        EXPECT_TRUE(set.contains(eid1));
        EXPECT_TRUE(set.contains(eid2));
    }

    /// @brief Test EntityId with InvalidValue in comparisons.
    TEST(EntityId, InvalidValue_LessThanComparison_BehavesCorrectly)
    {
        EcsRegistry reg;
        const EntityId invalid;
        const EntityId valid = reg.Create();

        // InvalidValue should be largest value
        EXPECT_TRUE(valid < invalid);
        EXPECT_FALSE(invalid < valid);
    }

    // ==================== EcsComponentPool Tests ====================

    /// @brief Test adding a component to an entity.
    TEST(EcsComponentPool, Add_NewComponent_ReturnsTrue)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        ComponentA compA{ 42 };

        const bool added = pool.Add(eid, compA);
        EXPECT_TRUE(added);
        EXPECT_TRUE(pool.Has(eid));
        EXPECT_EQ(pool[eid], compA);
        EXPECT_EQ(pool.size(), 1);
    }

    /// @brief Test adding a duplicate component fails.
    TEST(EcsComponentPool, Add_DuplicateComponent_ReturnsFalse)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        ComponentA compA{ 42 };

        pool.Add(eid, compA);
        assert_capture(capture)
        {
            const bool addedAgain = pool.Add(eid, ComponentA{ 100 });
            EXPECT_FALSE(addedAgain);
            EXPECT_TRUE(capture.Message().contains("Component already exists for entity"));
        }
        EXPECT_EQ(pool[eid], compA); // Original value unchanged
    }

    /// @brief Test pool capacity management with many adds.
    TEST(EcsComponentPool, Add_ManyComponents_HandlesResizing)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        std::vector<EntityId> eids;
        constexpr int COUNT = 10000;

        for (int i = 0; i < COUNT; ++i)
        {
            const auto eid = reg.Create();
            eids.push_back(eid);
            pool.Add(eid, ComponentA{ i });
        }

        EXPECT_EQ(pool.size(), COUNT);

        for (int i = 0; i < COUNT; ++i)
        {
            auto comp = pool[eids[i]];
            EXPECT_EQ(comp.a, i);
        }
    }

    /// @brief Test removing a component.
    TEST(EcsComponentPool, Remove_ExistingComponent_ComponentRemoved)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        ComponentA compA{ 42 };

        pool.Add(eid, compA);
        EXPECT_TRUE(pool.Has(eid));

        pool.Remove(eid);
        EXPECT_FALSE(pool.Has(eid));
        EXPECT_EQ(pool.size(), 0);
    }

    /// @brief Test removing a non-existent component has no effect.
    TEST(EcsComponentPool, Remove_NonExistentComponent_NoEffect)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();

        EXPECT_FALSE(pool.Has(eid));
        pool.Remove(eid); // Should not crash
        EXPECT_FALSE(pool.Has(eid));
        EXPECT_EQ(pool.size(), 0);
    }

    /// @brief Test removing and re-adding to same entity.
    TEST(EcsComponentPool, Remove_ThenAdd_Succeeds)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        ComponentA compA{ 42 };
        ComponentA compB{ 100 };

        pool.Add(eid, compA);
        EXPECT_EQ(pool[eid], compA);

        pool.Remove(eid);
        EXPECT_FALSE(pool.Has(eid));

        const bool addedReAdd = pool.Add(eid, compB);
        EXPECT_TRUE(addedReAdd);
        EXPECT_EQ(pool[eid], compB);
    }

    /// @brief Test remove maintains correct associations for remaining entities.
    TEST(EcsComponentPool, Remove_MiddleEntity_MaintainsCorrectAssociations)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        std::vector<EntityId> eids;
        for (int i = 0; i < 5; ++i)
        {
            const auto eid = reg.Create();
            eids.push_back(eid);
            pool.Add(eid, ComponentA{ i * 10 });
        }

        pool.Remove(eids[2]);

        EXPECT_FALSE(pool.Has(eids[2]));
        EXPECT_EQ(pool.size(), 4);

        for (int i = 0; i < 5; ++i)
        {
            if (i != 2)
            {
                auto comp = pool[eids[i]];
                EXPECT_EQ(comp.a, i * 10);
            }
        }
    }

    /// @brief Test operator[] returns pointer to component.
    TEST(EcsComponentPool, Indexer_ExistingComponent_ReturnsValidPointer)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        ComponentA compA{ 42 };
        
        pool.Add(eid, compA);

        auto comp = pool[eid];
        EXPECT_EQ(comp, compA);
    }

    /// @brief Test const operator[] method.
    TEST(EcsComponentPool, Indexer_ConstVersion_ReturnsConstPointer)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        pool.Add(eid, ComponentA{ 42 });

        const EcsComponentPool<ComponentA>& constPool = pool;
        auto comp = constPool[eid];

        EXPECT_EQ(comp, ComponentA{ 42 });
    }

    /// @brief Test Has with existing component.
    TEST(EcsComponentPool, Has_ExistingComponent_ReturnsTrue)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        pool.Add(eid, ComponentA{ 42 });

        EXPECT_TRUE(pool.Has(eid));
    }

    /// @brief Test Has with non-existent component.
    TEST(EcsComponentPool, Has_NonExistentComponent_ReturnsFalse)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();

        EXPECT_FALSE(pool.Has(eid));
    }

    /// @brief Test Has with boundary EntityIds.
    TEST(EcsComponentPool, Has_BoundaryEntityIds_HandlesCorrectly)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid1 = reg.Create(); // Should be 0
        const auto eid2 = reg.Create(); // Should be 1
        const auto eid3 = reg.Create(); // Should be 2

        EXPECT_EQ(eid1.Value(), 0);
        EXPECT_EQ(eid2.Value(), 1);
        EXPECT_EQ(eid3.Value(), 2);

        pool.Add(eid1, ComponentA{ 1 });
        pool.Add(eid3, ComponentA{ 3 });

        EXPECT_TRUE(pool.Has(eid1));
        EXPECT_FALSE(pool.Has(eid2));
        EXPECT_TRUE(pool.Has(eid3));
    }

    /// @brief Test size returns correct count.
    TEST(EcsComponentPool, Size_AfterOperations_ReturnsCorrectCount)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        EXPECT_EQ(pool.size(), 0);

        const auto eid1 = reg.Create();
        pool.Add(eid1, ComponentA{ 1 });
        EXPECT_EQ(pool.size(), 1);

        const auto eid2 = reg.Create();
        pool.Add(eid2, ComponentA{ 2 });
        EXPECT_EQ(pool.size(), 2);

        pool.Remove(eid1);
        EXPECT_EQ(pool.size(), 1);

        pool.Remove(eid2);
        EXPECT_EQ(pool.size(), 0);
    }

    /// @brief Test iterator functionality for begin/end.
    TEST(EcsComponentPool, Iterator_BeginEnd_IteratesAllEntities)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        std::vector<EntityId> eids;
        for (int i = 0; i < 10; ++i)
        {
            const auto eid = reg.Create();
            eids.push_back(eid);
            pool.Add(eid, ComponentA{ i });
        }

        std::vector<EntityId> iteratedEids;
        for (auto it = pool.begin(); it != pool.end(); ++it)
        {
            iteratedEids.push_back(*it);
        }

        EXPECT_EQ(iteratedEids.size(), eids.size());

        // Both lists should contain same entity IDs (order may differ)
        std::sort(eids.begin(), eids.end());
        std::sort(iteratedEids.begin(), iteratedEids.end());
        EXPECT_EQ(iteratedEids, eids);
    }

    /// @brief Test range-based for loop with iterator.
    TEST(EcsComponentPool, Iterator_RangeBasedFor_IteratesAllEntities)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        std::vector<EntityId> eids;
        for (int i = 0; i < 10; ++i)
        {
            const auto eid = reg.Create();
            eids.push_back(eid);
            pool.Add(eid, ComponentA{ i });
        }

        std::vector<EntityId> iteratedEids;
        for (auto eid : pool)
        {
            iteratedEids.push_back(eid);
        }

        EXPECT_EQ(iteratedEids.size(), eids.size());

        std::sort(eids.begin(), eids.end());
        std::sort(iteratedEids.begin(), iteratedEids.end());
        EXPECT_EQ(iteratedEids, eids);
    }

    /// @brief Test index consistency after mass add/remove operations.
    TEST(EcsComponentPool, IndexConsistency_MassOperations_RemainsConsistent)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentC> pool;

        std::vector<EntityId> eids;
        constexpr int COUNT = 1000;

        for (int i = 0; i < COUNT; ++i)
        {
            const auto eid = reg.Create();
            eids.push_back(eid);
            pool.Add(eid, ComponentC{ static_cast<float>(i), static_cast<float>(i), static_cast<float>(i),
                                       std::to_string(i), i });
        }

        // Remove every other one
        for (int i = 0; i < COUNT; i += 2)
        {
            pool.Remove(eids[i]);
        }

        EXPECT_EQ(pool.size(), COUNT / 2);

        // Verify remaining entities
        for (int i = 1; i < COUNT; i += 2)
        {
            auto comp = pool[eids[i]];
            EXPECT_EQ(comp.n, i);
        }

        // Verify removed entities
        for (int i = 0; i < COUNT; i += 2)
        {
            EXPECT_FALSE(pool.Has(eids[i]));
        }
    }    
    
    // ==================== EcsRegistry Tests ====================

    /// @brief Confirm when a registry creates a new entity ID that it is valid and alive.
    TEST(EcsRegistry, Create_NewEntity_EntityIsAlive)
    {
        constexpr int NUM_TO_CREATE = 10;

        EcsRegistry reg;

        std::vector<EntityId> eids(NUM_TO_CREATE);
        for (auto& eid : eids)
        {
            eid = reg.Create();
            EXPECT_TRUE(eid.IsValid());
        }

        // All eids should be unique
        std::set<EntityId> unique(eids.begin(), eids.end());
        EXPECT_EQ(unique.size(), eids.size());

        // All eids should be alive
        for (auto eid : eids)
        {
            EXPECT_TRUE(reg.IsAlive(eid));
        }
    }

    /// @brief Confirm when a registry destroys an entity ID that it is no longer alive.
    TEST(EcsRegistry, Destroy_Entity_EntityNotAlive)
    {
        EcsRegistry reg;

        auto eids = CreateEntityIds(reg);

        for (auto eid : eids)
        {
            reg.Destroy(eid);
        }

        for (auto eid : eids)
        {
            EXPECT_FALSE(reg.IsAlive(eid));
        }
    }

    /// @brief Confirm when entity IDs are destroyed and new ones created that
    /// the destroyed IDs are recycled.
    TEST(EcsRegistry, Create_AfterDestroy_EntityIdsRecycled)
    {
        EcsRegistry reg;

        auto eids1 = CreateEntityIds(reg);

        for (auto eid : eids1)
        {
            reg.Destroy(eid);
        }

        auto eids2 = CreateEntityIds(reg);

        std::set<EntityId> unique1(eids1.begin(), eids1.end());
        std::set<EntityId> unique2(eids2.begin(), eids2.end());

        EXPECT_EQ(unique1, unique2);
    }

    /// @brief Test IsAlive returns false for non-existent entities.
    TEST(EcsRegistry, IsAlive_NonExistentEntity_ReturnsFalse)
    {
        EcsRegistry reg;
        EntityId eid;

        EXPECT_FALSE(reg.IsAlive(eid));
    }

    /// @brief Test mixed create/destroy/recycle patterns.
    TEST(EcsRegistry, Create_MixedDestroyCreateSequence_Consistent)
    {
        EcsRegistry reg;

        std::vector<EntityId> alive;

        for (int i = 0; i < 10; ++i)
        {
            alive.push_back(reg.Create());
        }

        reg.Destroy(alive[2]);
        reg.Destroy(alive[5]);
        alive.erase(alive.begin() + 5);
        alive.erase(alive.begin() + 2);

        EntityId recycled1 = reg.Create();
        EntityId recycled2 = reg.Create();

        EXPECT_TRUE(reg.IsAlive(recycled1));
        EXPECT_TRUE(reg.IsAlive(recycled2));

        for (auto eid : alive)
        {
            EXPECT_TRUE(reg.IsAlive(eid));
        }
    }

    /// @brief Test adding a component to an entity.
    TEST(EcsRegistry, Add_Component_ComponentAdded)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();

        const bool added = reg.Add<ComponentA>(eid, compA);

        EXPECT_TRUE(added);
        EXPECT_TRUE(reg.Has<ComponentA>(eid));
    }

    /// @brief Test adding a component to a dead entity fails.
    TEST(EcsRegistry, Add_ComponentToDeadEntity_ReturnsFalse)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        reg.Destroy(eid);

        auto compA = RandomValue<ComponentA>();
        assert_capture(capture)
        {
            const bool added = reg.Add<ComponentA>(eid, compA);
            EXPECT_FALSE(added);
            EXPECT_TRUE(capture.Message().contains("Entity is not alive"));
        }
        
        EXPECT_FALSE(reg.Has<ComponentA>(eid));
    }

    /// @brief Confirm adding components to entities works and the correct
    /// components are returned.
    TEST(EcsRegistry, Add_MultipleComponents_CorrectComponentsReturned)
    {
        EcsRegistry reg;

        auto eids = CreateEntityIds(reg);

        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            auto result = reg.Add<ComponentC>(eid, c);
            EXPECT_TRUE(result);
        }

        for (auto eid : eids)
        {
            EXPECT_TRUE(reg.Has<ComponentC>(eid));

            const auto& expected = Cs[eid];
            const auto actual = reg.GetHandle<ComponentC>(eid);

            EXPECT_TRUE(actual);
            EXPECT_EQ(actual->Get<ComponentC>(), expected);
        }
    }

    /// @brief Confirm adding a component to an entity that already has that component fails.
    TEST(EcsRegistry, Add_DuplicateComponent_ReturnsFalse)
    {
        EcsRegistry reg;

        auto eids = CreateEntityIds(reg);

        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            reg.Add<ComponentC>(eid, c);
        }

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            assert_capture(capture)
            {
                const auto result = reg.Add<ComponentC>(eid, c);
                EXPECT_FALSE(result);
                EXPECT_TRUE(capture.Message().contains("Component already exists for entity"));
            }
        }
    }

    /// @brief Test adding multiple different component types to same entity.
    TEST(EcsRegistry, Add_DifferentTypesToSameEntity_AllAccessible)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        auto compD = RandomValue<ComponentD>();

        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);
        reg.Add<ComponentD>(eid, compD);

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_TRUE(reg.Has<ComponentB>(eid));
        EXPECT_TRUE(reg.Has<ComponentC>(eid));
        EXPECT_TRUE(reg.Has<ComponentD>(eid));

        EXPECT_EQ(reg.GetHandle<ComponentA>(eid)->Get<ComponentA>(), compA);
        EXPECT_EQ(reg.GetHandle<ComponentB>(eid)->Get<ComponentB>(), compB);
        EXPECT_EQ(reg.GetHandle<ComponentC>(eid)->Get<ComponentC>(), compC);
        EXPECT_EQ(reg.GetHandle<ComponentD>(eid)->Get<ComponentD>(), compD);
    }

    /// @brief Test that destroying entity removes all component types.
    TEST(EcsRegistry, Destroy_Entity_AllComponentsRemoved)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
        reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
        reg.Add<ComponentD>(eid, RandomValue<ComponentD>());

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_TRUE(reg.Has<ComponentB>(eid));
        EXPECT_TRUE(reg.Has<ComponentC>(eid));
        EXPECT_TRUE(reg.Has<ComponentD>(eid));

        reg.Destroy(eid);

        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        EXPECT_FALSE(reg.Has<ComponentB>(eid));
        EXPECT_FALSE(reg.Has<ComponentC>(eid));
        EXPECT_FALSE(reg.Has<ComponentD>(eid));
    }

    /// @brief Test that when an entity is recycled, it has no components.
    TEST(EcsRegistry, RecycleEntity_NoComponentsOnRecycledEntity)
    {
        EcsRegistry reg;

        const auto eid = reg.Create();

        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
        reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
        reg.Add<ComponentD>(eid, RandomValue<ComponentD>());

        reg.Destroy(eid);

        const auto newEid = reg.Create();

        EXPECT_FALSE(reg.Has<ComponentA>(newEid));
        EXPECT_FALSE(reg.Has<ComponentB>(newEid));
        EXPECT_FALSE(reg.Has<ComponentC>(newEid));
        EXPECT_FALSE(reg.Has<ComponentD>(newEid));
    }

    /// @brief Test recycled entity with new components has correct values.
    TEST(EcsRegistry, RecycleEntity_AddNewComponents_CorrectValuesReturned)
    {
        EcsRegistry reg;

        const auto eid = reg.Create();

        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
        reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
        reg.Add<ComponentD>(eid, RandomValue<ComponentD>());

        reg.Destroy(eid);

        const auto newEid = reg.Create();

        auto newCompA = RandomValue<ComponentA>();
        auto newCompB = RandomValue<ComponentB>();
        auto newCompC = RandomValue<ComponentC>();
        auto newCompD = RandomValue<ComponentD>();

        reg.Add<ComponentA>(newEid, newCompA);
        reg.Add<ComponentB>(newEid, newCompB);
        reg.Add<ComponentC>(newEid, newCompC);
        reg.Add<ComponentD>(newEid, newCompD);

        EXPECT_TRUE(reg.Has<ComponentA>(newEid));
        EXPECT_TRUE(reg.Has<ComponentB>(newEid));
        EXPECT_TRUE(reg.Has<ComponentC>(newEid));
        EXPECT_TRUE(reg.Has<ComponentD>(newEid));

        EXPECT_EQ(reg.GetHandle<ComponentA>(newEid)->Get<ComponentA>(), newCompA);
        EXPECT_EQ(reg.GetHandle<ComponentB>(newEid)->Get<ComponentB>(), newCompB);
        EXPECT_EQ(reg.GetHandle<ComponentC>(newEid)->Get<ComponentC>(), newCompC);
        EXPECT_EQ(reg.GetHandle<ComponentD>(newEid)->Get<ComponentD>(), newCompD);
    }

    /// @brief Test component access after entity destruction.
    TEST(EcsRegistry, GetComponent_AfterDestroy_ReturnsError)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        reg.Add<ComponentA>(eid, ComponentA{ 42 });

        EXPECT_TRUE(reg.Has<ComponentA>(eid));

        reg.Destroy(eid);

        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        
        assert_capture(capture)
        {
            auto comp = reg.GetHandle<ComponentA>(eid);
            EXPECT_FALSE(comp);
            EXPECT_TRUE(capture.Message().contains("IsAlive(eid)"));
        }
    }

    /// @brief Test replacing components through the reference returned by Get.
    TEST(EcsRegistry, Get_ModifyThroughReference_ComponentUpdated)
    {
        EcsRegistry reg;

        auto eids = CreateEntityIds(reg);

        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            reg.Add<ComponentC>(eid, c);
        }

        std::unordered_map<EntityId, ComponentC> newCs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            newCs.emplace(eid, c);
            reg.GetHandle<ComponentC>(eid)->Get<ComponentC>() = c;
        }

        for (auto eid : eids)
        {
            EXPECT_TRUE(reg.Has<ComponentC>(eid));

            const auto& expected = newCs[eid];
            const auto actual = reg.GetHandle<ComponentC>(eid);

            EXPECT_TRUE(actual);
            EXPECT_EQ(actual->Get<ComponentC>(), expected);
        }
    }

    /// @brief Test Get<C> with entity that has no components.
    TEST(EcsRegistry, Get_EntityWithNoComponents_ReturnsError)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto component = reg.GetHandle<ComponentA>(eid);

        EXPECT_FALSE(component);
    }

    /// @brief Confirm an entity with associated components that is recycled no longer has old components.
    TEST(EcsRegistry, RecycleEntity_OldComponentsRemoved)
    {
        EcsRegistry reg;

        auto eids = CreateEntityIds(reg);

        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            reg.Add<ComponentC>(eid, c);
        }

        const auto eidToRecycle = eids[eids.size() / 2];
        reg.Destroy(eidToRecycle);

        EXPECT_FALSE(reg.Has<ComponentC>(eidToRecycle));

        const auto newEid = reg.Create();

        EXPECT_EQ(eidToRecycle, newEid);

        EXPECT_FALSE(reg.Has<ComponentC>(eidToRecycle));

        EXPECT_FALSE(reg.GetHandle<ComponentC>(eidToRecycle));

        auto newC = RandomValue<ComponentC>();
        reg.Add<ComponentC>(newEid, newC);

        EXPECT_TRUE(reg.Has<ComponentC>(eidToRecycle));

        EXPECT_EQ(reg.GetHandle<ComponentC>(newEid)->Get<ComponentC>(), newC);
    }

    /// @brief Helper to populate registry with entities with multiple components.
    template<typename... Cs>
    static std::vector<EntityId> PopulateRegistryWithComponents(
        EcsRegistry& reg,
        std::unordered_map<EntityId, std::tuple<Cs...>>& components)
    {
        std::vector<EntityId> eids = CreateEntityIds(reg);

        for (auto eid : eids)
        {
            auto tuple = std::make_tuple(RandomValue<Cs>()...);
            (reg.Add<Cs>(eid, std::get<Cs>(tuple)), ...);
            components.emplace(eid, tuple);
        }

        return eids;
    }

    /// @brief Helper to verify that views return the expected components.
    template<typename... Cs>
    static void VerifyViewComponents(
        EcsRegistry& reg,
        std::unordered_map<EntityId, std::tuple<Cs...>>& expectedComponents)
    {
        for (auto& [eid, expectedTuple] : expectedComponents)
        {
            auto handle = reg.GetHandle<Cs...>(eid);
            EXPECT_TRUE(handle);

            auto actualTuple = std::tuple<Cs&...>(handle->Get<Cs>()...);

            EXPECT_EQ(actualTuple, expectedTuple);
        }
    }

    /// @brief Confirm viewing entities with multiple components works correctly.
    TEST(EcsRegistry, View_Iteration_CorrectComponentsReturned)
    {
        EcsRegistry reg;

        std::unordered_map<EntityId, std::tuple<ComponentA>> componentsA;
        std::unordered_map<EntityId, std::tuple<ComponentB>> componentsB;
        std::unordered_map<EntityId, std::tuple<ComponentC>> componentsC;
        std::unordered_map<EntityId, std::tuple<ComponentA, ComponentB>> componentsAB;
        std::unordered_map<EntityId, std::tuple<ComponentA, ComponentC>> componentsAC;
        std::unordered_map<EntityId, std::tuple<ComponentB, ComponentC>> componentsBC;
        std::unordered_map<EntityId, std::tuple<ComponentA, ComponentB, ComponentC>> componentsABC;

        //Populate registry with various combinations of components
        auto eidsA = PopulateRegistryWithComponents<ComponentA>(reg, componentsA);
        auto eidsB = PopulateRegistryWithComponents<ComponentB>(reg, componentsB);
        auto eidsC = PopulateRegistryWithComponents<ComponentC>(reg, componentsC);
        auto eidsAB = PopulateRegistryWithComponents<ComponentA, ComponentB>(reg, componentsAB);
        auto eidsAC = PopulateRegistryWithComponents<ComponentA, ComponentC>(reg, componentsAC);
        auto eidsBC = PopulateRegistryWithComponents<ComponentB, ComponentC>(reg, componentsBC);
        auto eidsABC = PopulateRegistryWithComponents<ComponentA, ComponentB, ComponentC>(reg, componentsABC);

        //Merge component maps to ensure all combinations are tested
        for(auto comp : componentsAB)
        {
            componentsA.emplace(comp.first, std::make_tuple(std::get<0>(comp.second)));
            componentsB.emplace(comp.first, std::make_tuple(std::get<1>(comp.second)));
        }
        for(auto comp : componentsAC)
        {
            componentsA.emplace(comp.first, std::make_tuple(std::get<0>(comp.second)));
            componentsC.emplace(comp.first, std::make_tuple(std::get<1>(comp.second)));
        }
        for(auto comp : componentsBC)
        {
            componentsB.emplace(comp.first, std::make_tuple(std::get<0>(comp.second)));
            componentsC.emplace(comp.first, std::make_tuple(std::get<1>(comp.second)));
        }
        for(auto comp : componentsABC)
        {
            componentsA.emplace(comp.first, std::make_tuple(std::get<0>(comp.second)));
            componentsB.emplace(comp.first, std::make_tuple(std::get<1>(comp.second)));
            componentsC.emplace(comp.first, std::make_tuple(std::get<2>(comp.second)));
            componentsAB.emplace(comp.first, std::make_tuple(std::get<0>(comp.second), std::get<1>(comp.second)));
            componentsAC.emplace(comp.first, std::make_tuple(std::get<0>(comp.second), std::get<2>(comp.second)));
            componentsBC.emplace(comp.first, std::make_tuple(std::get<1>(comp.second), std::get<2>(comp.second)));
        }

        //Verify views
        VerifyViewComponents(reg, componentsA);
        VerifyViewComponents(reg, componentsB);
        VerifyViewComponents(reg, componentsC);
        VerifyViewComponents(reg, componentsAB);
        VerifyViewComponents(reg, componentsAC);
        VerifyViewComponents(reg, componentsBC);
        VerifyViewComponents(reg, componentsABC);
    }

    // ==================== View Tests ====================

    /// @brief Test View with empty results.
    TEST(View, EmptyResults_NoEntityMatches_IterationEmpty)
    {
        EcsRegistry reg;

        // Create entities with ComponentA only
        for (int i = 0; i < 5; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        }

        // View for ComponentB should return nothing
        int count = 0;
        for (auto row : reg.GetView<ComponentB>())
        {
            count++;
        }

        EXPECT_EQ(count, 0);
    }

    /// @brief Test View with full results.
    TEST(View, FullResults_AllEntitiesMatch_IterationFull)
    {
        EcsRegistry reg;

        constexpr int NUM_ENTITIES = 10;

        // Create entities all with ComponentA
        std::vector<EntityId> eids;
        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
            eids.push_back(eid);
        }

        // View for ComponentA should return all
        int count = 0;
        std::vector<EntityId> resultSet;
        for (auto row : reg.GetView<ComponentA>())
        {
            const auto eid = std::get<0>(row);
            resultSet.push_back(eid);
            count++;
        }

        EXPECT_EQ(count, NUM_ENTITIES);
        std::sort(eids.begin(), eids.end());
        std::sort(resultSet.begin(), resultSet.end());
        EXPECT_EQ(eids, resultSet);
    }

    /// @brief Test View with partial results.
    TEST(View, PartialResults_SomeEntitiesMatch_IterationPartial)
    {
        EcsRegistry reg;

        std::vector<EntityId> withA;
        std::vector<EntityId> withAB;

        // Create entities with ComponentA only
        for (int i = 0; i < 5; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
            withA.push_back(eid);
        }

        // Create entities with ComponentA and ComponentB
        for (int i = 0; i < 7; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
            reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
            withAB.push_back(eid);
        }

        // View for ComponentA and ComponentB should return 7
        int count = 0;
        std::vector<EntityId> resultSet;
        for (auto row : reg.GetView<ComponentA, ComponentB>())
        {
            const auto eid = std::get<0>(row);
            resultSet.push_back(eid);
            count++;
        }

        EXPECT_EQ(count, 7);
        std::sort(withAB.begin(), withAB.end());
        std::sort(resultSet.begin(), resultSet.end());
        EXPECT_EQ(withAB, resultSet);
    }

    /// @brief Test multiple View iterations.
    TEST(View, MultipleIterations_CallGetViewRepeatedly_ConsistentResults)
    {
        EcsRegistry reg;

        // Create mixed entities
        for (int i = 0; i < 10; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
            if (i % 2 == 0)
            {
                reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
            }
        }

        // Iterate multiple times
        std::vector<EntityId> results1;
        for (auto row : reg.GetView<ComponentA, ComponentB>())
        {
            const auto eid = std::get<0>(row);
            results1.push_back(eid);
        }

        std::vector<EntityId> results2;
        for (auto row : reg.GetView<ComponentA, ComponentB>())
        {
            const auto eid = std::get<0>(row);
            results2.push_back(eid);
        }

        std::sort(results1.begin(), results1.end());
        std::sort(results2.begin(), results2.end());

        EXPECT_EQ(results1, results2);
    }

    /// @brief Test View with single component.
    TEST(View, SingleComponent_View_ReturnsCorrectEntities)
    {
        EcsRegistry reg;

        std::vector<EntityId> withA;
        std::vector<EntityId> withoutA;

        // Create entities with ComponentA
        for (int i = 0; i < 5; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
            withA.push_back(eid);
        }

        // Create entities without ComponentA
        for (int i = 0; i < 3; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
            withoutA.push_back(eid);
        }

        // View for ComponentA
        int count = 0;
        std::vector<EntityId> resultSet;
        for (auto row : reg.GetView<ComponentA>())
        {
            const auto eid = std::get<0>(row);
            resultSet.push_back(eid);
            count++;
        }

        EXPECT_EQ(count, 5);
        std::sort(withA.begin(), withA.end());
        std::sort(resultSet.begin(), resultSet.end());
        EXPECT_EQ(withA, resultSet);
    }

    /// @brief Test View consistency with manual checks.
    TEST(View, Consistency_ViewVsManualCheck_MatchResults)
    {
        EcsRegistry reg;

        // Create diverse entity set
        for (int i = 0; i < 20; ++i)
        {
            auto eid = reg.Create();
            if (i % 3 == 0) reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
            if (i % 2 == 0) reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
            if (i % 5 == 0) reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
        }

        // Get all alive entities
        std::vector<EntityId> allAlive;
        for (auto row : reg.GetView<ComponentA>())
        {
            const auto eid = std::get<0>(row);
            allAlive.push_back(eid);
        }

        // Manually check each
        std::vector<EntityId> manualCheck;
        for (auto eid : allAlive)
        {
            if (reg.Has<ComponentA>(eid) && reg.Has<ComponentB>(eid))
            {
                manualCheck.push_back(eid);
            }
        }

        // View for A and B
        std::vector<EntityId> resultSet;
        for (auto row : reg.GetView<ComponentA, ComponentB>())
        {
            const auto eid = std::get<0>(row);
            resultSet.push_back(eid);
        }

        std::sort(manualCheck.begin(), manualCheck.end());
        std::sort(resultSet.begin(), resultSet.end());
        EXPECT_EQ(resultSet, manualCheck);
    }

    // ==================== Stress/Integration Tests ====================

    /// @brief Stress test with large entity population.
    TEST(StressTest, LargePopulation_ManyEntitiesAndComponents_Stable)
    {
        EcsRegistry reg;

        constexpr int NUM_ENTITIES = 5000;

        std::vector<EntityId> eids;

        // Create and populate
        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            auto eid = reg.Create();
            eids.push_back(eid);

            reg.Add<ComponentA>(eid, ComponentA{ i });
            if (i % 2 == 0) reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
            if (i % 3 == 0) reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
            if (i % 5 == 0) reg.Add<ComponentD>(eid, RandomValue<ComponentD>());
        }

        // View and verify
        int countA = 0, countAB = 0, countABC = 0;

        for (auto row : reg.GetView<ComponentA>()) { countA++; }
        for (auto row : reg.GetView<ComponentA, ComponentB>()) { countAB++; }
        for (auto row : reg.GetView<ComponentA, ComponentB, ComponentC>()) { countABC++; }

        EXPECT_EQ(countA, NUM_ENTITIES);
        EXPECT_EQ(countAB, NUM_ENTITIES / 2);
        EXPECT_EQ(countABC, (NUM_ENTITIES / 6) + 1);
    }

    /// @brief Test add/remove patterns repeatedly.
    TEST(StressTest, AddRemoveCycles_RepeatedCycles_MaintainsConsistency)
    {
        EcsRegistry reg;

        for (int cycle = 0; cycle < 10; ++cycle)
        {
            std::vector<EntityId> eids;

            // Create entities
            for (int i = 0; i < 100; ++i)
            {
                auto eid = reg.Create();
                eids.push_back(eid);
                reg.Add<ComponentA>(eid, ComponentA{ i });
            }

            // Destroy half
            for (int i = 0; i < 50; ++i)
            {
                reg.Destroy(eids[i]);
            }

            // Verify remaining
            for (int i = 50; i < 100; ++i)
            {
                EXPECT_TRUE(reg.IsAlive(eids[i]));
                EXPECT_TRUE(reg.Has<ComponentA>(eids[i]));
            }

            // Verify destroyed
            for (int i = 0; i < 50; ++i)
            {
                EXPECT_FALSE(reg.IsAlive(eids[i]));
            }
        }
    }

    /// @brief Test multiple component pool cohabitation.
    TEST(StressTest, PoolCohabitation_ManyComponentTypes_NoInterference)
    {
        EcsRegistry reg;

        constexpr int NUM_ENTITIES = 1000;

        // Create entities with all component types
        std::vector<EntityId> eids;
        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            auto eid = reg.Create();
            eids.push_back(eid);

            reg.Add<ComponentA>(eid, ComponentA{ i });
            reg.Add<ComponentB>(eid, ComponentB{ static_cast<float>(i), static_cast<float>(i), static_cast<float>(i) });
            reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
            reg.Add<ComponentD>(eid, RandomValue<ComponentD>());
        }

        // Verify pools don't interfere
        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            auto compAResult = reg.GetHandle<ComponentA>(eids[i]);
            EXPECT_TRUE(compAResult);
            auto compA = compAResult->Get<ComponentA>();
            EXPECT_EQ(compA.a, i);

            auto compBResult = reg.GetHandle<ComponentB>(eids[i]);
            EXPECT_TRUE(compBResult);
            auto compB = compBResult->Get<ComponentB>();
            EXPECT_EQ(compB.x, static_cast<float>(i));

            auto compCResult = reg.GetHandle<ComponentC>(eids[i]);
            EXPECT_TRUE(compCResult);

            auto compDResult = reg.GetHandle<ComponentD>(eids[i]);
            EXPECT_TRUE(compDResult);
        }
    }

    /// @brief Test Has with existing component returns true.
    TEST(EcsRegistry, Has_ComponentExists_ReturnsTrue)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
    }

    /// @brief Test Has with non-existent component returns false.
    TEST(EcsRegistry, Has_ComponentDoesNotExist_ReturnsFalse)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        EXPECT_FALSE(reg.Has<ComponentA>(eid));
    }

    /// @brief Test Has with dead entity returns false.
    TEST(EcsRegistry, Has_DeadEntity_ReturnsFalse)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        reg.Destroy(eid);

        EXPECT_FALSE(reg.Has<ComponentA>(eid));
    }

    /// @brief Test removing a specific component type from an entity.
    TEST(EcsRegistry, Remove_SpecificComponent_OnlyThatComponentRemoved)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_TRUE(reg.Has<ComponentB>(eid));
        EXPECT_TRUE(reg.Has<ComponentC>(eid));

        reg.Remove<ComponentB>(eid);

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_FALSE(reg.Has<ComponentB>(eid));
        EXPECT_TRUE(reg.Has<ComponentC>(eid));

        EXPECT_EQ(reg.GetHandle<ComponentA>(eid)->Get<ComponentA>(), compA);
        EXPECT_EQ(reg.GetHandle<ComponentC>(eid)->Get<ComponentC>(), compC);
    }

    /// @brief Test removing a component that doesn't exist.
    TEST(EcsRegistry, Remove_NonexistentComponent_NoEffect)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        
        reg.Add<ComponentA>(eid, compA);
        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_FALSE(reg.Has<ComponentB>(eid));

        reg.Remove<ComponentB>(eid);

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_EQ(reg.GetHandle<ComponentA>(eid)->Get<ComponentA>(), compA);
        EXPECT_FALSE(reg.Has<ComponentB>(eid));
    }

    /// @brief Test removing a component when entity only has that component.
    TEST(EcsRegistry, Remove_OnlyComponent_EntityHasNoComponentsAfter)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        
        reg.Add<ComponentA>(eid, compA);
        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_TRUE(reg.IsAlive(eid));

        reg.Remove<ComponentA>(eid);

        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        EXPECT_TRUE(reg.IsAlive(eid));
    }

    /// @brief Test sequential removal of all component types.
    TEST(EcsRegistry, Remove_AllComponentsSequentially_EntityAliveButEmpty)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
        reg.Add<ComponentC>(eid, RandomValue<ComponentC>());

        reg.Remove<ComponentA>(eid);
        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        EXPECT_TRUE(reg.Has<ComponentB>(eid));
        EXPECT_TRUE(reg.Has<ComponentC>(eid));

        reg.Remove<ComponentB>(eid);
        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        EXPECT_FALSE(reg.Has<ComponentB>(eid));
        EXPECT_TRUE(reg.Has<ComponentC>(eid));

        reg.Remove<ComponentC>(eid);
        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        EXPECT_FALSE(reg.Has<ComponentB>(eid));
        EXPECT_FALSE(reg.Has<ComponentC>(eid));

        EXPECT_TRUE(reg.IsAlive(eid));
    }

    /// @brief Test removing from multiple entities independently.
    TEST(EcsRegistry, Remove_MultipleEntitiesIndependent_CorrectRemoval)
    {
        EcsRegistry reg;

        auto eid1 = reg.Create();
        auto eid2 = reg.Create();
        auto eid3 = reg.Create();

        auto compA1 = RandomValue<ComponentA>();
        auto compA2 = RandomValue<ComponentA>();
        auto compA3 = RandomValue<ComponentA>();

        reg.Add<ComponentA>(eid1, compA1);
        reg.Add<ComponentA>(eid2, compA2);
        reg.Add<ComponentA>(eid3, compA3);

        reg.Remove<ComponentA>(eid2);

        EXPECT_TRUE(reg.Has<ComponentA>(eid1));
        EXPECT_FALSE(reg.Has<ComponentA>(eid2));
        EXPECT_TRUE(reg.Has<ComponentA>(eid3));

        EXPECT_EQ(reg.GetHandle<ComponentA>(eid1)->Get<ComponentA>(), compA1);
        EXPECT_EQ(reg.GetHandle<ComponentA>(eid3)->Get<ComponentA>(), compA3);
    }

    /// @brief Test removing component from dead entity.
    TEST(EcsRegistry, Remove_DeadEntity_NoEffect)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        
        reg.Add<ComponentA>(eid, compA);
        EXPECT_TRUE(reg.IsAlive(eid));

        reg.Destroy(eid);
        EXPECT_FALSE(reg.IsAlive(eid));

        assert_capture(capture)
        {
            reg.Remove<ComponentA>(eid);
            EXPECT_TRUE(capture.Message().contains("Entity is not alive"));
        }

        EXPECT_FALSE(reg.IsAlive(eid));
    }

    /// @brief Test remove and re-add component to same entity.
    TEST(EcsRegistry, Remove_ThenReaddComponent_NewValueAssigned)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto origValue = RandomValue<ComponentA>();
        reg.Add<ComponentA>(eid, origValue);
        EXPECT_EQ(reg.GetHandle<ComponentA>(eid)->Get<ComponentA>(), origValue);

        reg.Remove<ComponentA>(eid);
        EXPECT_FALSE(reg.Has<ComponentA>(eid));

        auto newValue = RandomValue<ComponentA>();
        reg.Add<ComponentA>(eid, newValue);

        EXPECT_TRUE(reg.Has<ComponentA>(eid));
        EXPECT_EQ(reg.GetHandle<ComponentA>(eid)->Get<ComponentA>(), newValue);
    }

    /// @brief Test that views update correctly after removal.
    TEST(EcsRegistry, Remove_Component_ViewIsCorrect)
    {
        EcsRegistry reg;

        auto eid1 = reg.Create();
        auto eid2 = reg.Create();

        reg.Add<ComponentA>(eid1, RandomValue<ComponentA>());
        reg.Add<ComponentB>(eid1, RandomValue<ComponentB>());

        reg.Add<ComponentA>(eid2, RandomValue<ComponentA>());
        reg.Add<ComponentB>(eid2, RandomValue<ComponentB>());

        int countBefore = 0;
        for (auto row : reg.GetView<ComponentA, ComponentB>())
        {
            countBefore++;
        }
        EXPECT_EQ(countBefore, 2);

        reg.Remove<ComponentB>(eid1);

        int countAfter = 0;
        for (auto row : reg.GetView<ComponentA, ComponentB>())
        {
            countAfter++;
        }
        EXPECT_EQ(countAfter, 1);
    }

    /// @brief Test stress: remove from many entities.
    TEST(EcsRegistry, Remove_ManyEntities_ConsistentState)
    {
        EcsRegistry reg;

        constexpr int NUM_ENTITIES = 1000;
        std::vector<EntityId> eids;

        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            auto eid = reg.Create();
            eids.push_back(eid);

            reg.Add<ComponentA>(eid, ComponentA{ i });
            reg.Add<ComponentB>(eid, RandomValue<ComponentB>());
            if (i % 2 == 0)
            {
                reg.Add<ComponentC>(eid, RandomValue<ComponentC>());
            }
        }

        for (auto eid : eids)
        {
            reg.Remove<ComponentA>(eid);
        }

        for (auto eid : eids)
        {
            EXPECT_FALSE(reg.Has<ComponentA>(eid));
            EXPECT_TRUE(reg.Has<ComponentB>(eid));
        }

        int countWithC = 0;
        for (auto eid : eids)
        {
            if (reg.Has<ComponentC>(eid))
            {
                countWithC++;
            }
        }
        EXPECT_EQ(countWithC, NUM_ENTITIES / 2);

        for (int i = 0; i < NUM_ENTITIES; i += 2)
        {
            reg.Remove<ComponentB>(eids[i]);
        }

        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            EXPECT_FALSE(reg.Has<ComponentA>(eids[i]));
            if (i % 2 == 0)
            {
                EXPECT_FALSE(reg.Has<ComponentB>(eids[i]));
            }
            else
            {
                EXPECT_TRUE(reg.Has<ComponentB>(eids[i]));
            }
        }
    }

    /// @brief Test that Get on multiple component types fails when required component missing.
    TEST(EcsRegistry, Get_RequiredComponentMissing_GetFails)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();

        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        auto viewResult1 = reg.GetHandle<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(viewResult1);

        reg.Remove<ComponentB>(eid);

        auto viewResult2 = reg.GetHandle<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(viewResult2);
    }

    // ==================== GetHandle Tests ====================

    /// @brief Test GetHandle with single component returns valid handle.
    TEST(GetHandle, SingleComponent_ReturnsValidHandle)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        
        reg.Add<ComponentA>(eid, compA);

        auto handleResult = reg.GetHandle<ComponentA>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        EXPECT_EQ(handle.GetEntityId(), eid);
        
        auto actualA = handle.Get<ComponentA>();
        EXPECT_EQ(actualA, compA);
    }

    /// @brief Test GetHandle with multiple components returns valid handle.
    TEST(GetHandle, MultipleComponents_ReturnsValidHandle)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        EXPECT_EQ(handle.GetEntityId(), eid);
        
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        auto actualC = handle.Get<ComponentC>();
        
        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualB, compB);
        EXPECT_EQ(actualC, compC);
    }

    /// @brief Test GetHandle with two components - common use case.
    TEST(GetHandle, TwoComponents_ReturnsCorrectHandle)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        
        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualB, compB);
    }

    /// @brief Test GetHandle allows component modification through Get.
    TEST(GetHandle, ModifyThroughHandle_ComponentsUpdated)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = ComponentA{ 10 };
        auto compB = ComponentB{ 1.0f, 2.0f, 3.0f };
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto& refA = handle.Get<ComponentA>();
        auto& refB = handle.Get<ComponentB>();
        
        refA.a = 99;
        refB.x = 10.0f;
        refB.y = 20.0f;
        refB.z = 30.0f;

        // Verify changes persisted in registry
        EXPECT_EQ(reg.GetHandle<ComponentA>(eid)->Get<ComponentA>().a, 99);
        EXPECT_EQ(reg.GetHandle<ComponentB>(eid)->Get<ComponentB>().x, 10.0f);
        EXPECT_EQ(reg.GetHandle<ComponentB>(eid)->Get<ComponentB>().y, 20.0f);
        EXPECT_EQ(reg.GetHandle<ComponentB>(eid)->Get<ComponentB>().z, 30.0f);
    }

    /// @brief Test GetHandle fails when entity is not alive.
    TEST(GetHandle, EntityNotAlive_ReturnsError)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        reg.Destroy(eid);

        assert_capture(capture)
        {
            auto handleResult = reg.GetHandle<ComponentA>(eid);
            EXPECT_FALSE(handleResult);
            EXPECT_TRUE(capture.Message().contains("IsAlive(eid)"));
            EXPECT_EQ(handleResult.error().Message, std::format("Entity {} is not alive", eid));
        }
    }

    /// @brief Test GetHandle fails when entity has no components.
    TEST(GetHandle, NoComponents_ReturnsError)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_FALSE(handleResult);
        EXPECT_EQ(handleResult.error().Message, std::format("Entity {} does not have all requested components", eid));
    }

    /// @brief Test GetHandle fails when entity is missing one or more components.
    TEST(GetHandle, MissingComponents_ReturnsError)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        
        reg.Add<ComponentA>(eid, compA);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_FALSE(handleResult);
        EXPECT_EQ(handleResult.error().Message, std::format("Entity {} does not have all requested components", eid));
    }

    /// @brief Test GetHandle succeeds when entity has more components than requested.
    TEST(GetHandle, ExtraComponents_SucceedsWithRequestedOnly)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        auto compD = RandomValue<ComponentD>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);
        reg.Add<ComponentD>(eid, compD);

        auto handleResult = reg.GetHandle<ComponentA, ComponentC>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto actualA = handle.Get<ComponentA>();
        auto actualC = handle.Get<ComponentC>();
        
        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualC, compC);
    }

    /// @brief Test GetHandle with all four component types.
    TEST(GetHandle, FourComponents_AllAccessible)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        auto compD = RandomValue<ComponentD>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);
        reg.Add<ComponentD>(eid, compD);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB, ComponentC, ComponentD>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        auto actualC = handle.Get<ComponentC>();
        auto actualD = handle.Get<ComponentD>();
        
        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualB, compB);
        EXPECT_EQ(actualC, compC);
        EXPECT_EQ(actualD, compD);
    }

    /// @brief Test GetHandle persists across registry mutations.
    TEST(GetHandle, AcrossPoolMutations_RemainsValid)
    {
        EcsRegistry reg;

        auto eid1 = reg.Create();
        auto eid2 = reg.Create();
        
        auto compA1 = ComponentA{ 10 };
        auto compB1 = ComponentB{ 1.0f, 2.0f, 3.0f };
        
        reg.Add<ComponentA>(eid1, compA1);
        reg.Add<ComponentB>(eid1, compB1);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid1);
        EXPECT_TRUE(handleResult);
        auto handle = *handleResult;

        // Mutate pools by adding/removing entities
        reg.Add<ComponentA>(eid2, ComponentA{ 20 });
        reg.Add<ComponentB>(eid2, ComponentB{ 4.0f, 5.0f, 6.0f });
        
        auto eid3 = reg.Create();
        reg.Add<ComponentA>(eid3, ComponentA{ 30 });
        reg.Add<ComponentB>(eid3, ComponentB{ 7.0f, 8.0f, 9.0f });
        
        reg.Destroy(eid2);

        // Handle should still be able to access components
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        
        EXPECT_EQ(actualA.a, 10);
        EXPECT_EQ(actualB.x, 1.0f);
    }

    /// @brief Test GetHandle with multiple entities - each gets correct components.
    TEST(GetHandle, MultipleEntities_EachGetsCorrectHandle)
    {
        EcsRegistry reg;

        std::unordered_map<EntityId, std::tuple<ComponentA, ComponentB>> components;
        std::vector<EntityId> eids;

        for (int i = 0; i < 10; ++i)
        {
            auto eid = reg.Create();
            eids.push_back(eid);
            
            auto compA = ComponentA{ i * 10 };
            auto compB = ComponentB{ static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3) };
            
            reg.Add<ComponentA>(eid, compA);
            reg.Add<ComponentB>(eid, compB);
            
            components.emplace(eid, std::make_tuple(compA, compB));
        }

        for (const auto& [eid, expected] : components)
        {
            auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
            EXPECT_TRUE(handleResult);

            auto handle = *handleResult;
            
            auto actualA = handle.Get<ComponentA>();
            auto actualB = handle.Get<ComponentB>();
            
            auto [expectedA, expectedB] = expected;
            
            EXPECT_EQ(actualA, expectedA);
            EXPECT_EQ(actualB, expectedB);
        }
    }

    /// @brief Test GetHandle with recycled entity ID - should work correctly.
    TEST(GetHandle, RecycledEntityId_ReturnsNewComponents)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA1 = ComponentA{ 100 };
        
        reg.Add<ComponentA>(eid, compA1);
        reg.Destroy(eid);

        auto newEid = reg.Create();
        EXPECT_EQ(eid, newEid);

        auto compA2 = ComponentA{ 200 };
        auto compB2 = ComponentB{ 1.0f, 2.0f, 3.0f };
        
        reg.Add<ComponentA>(newEid, compA2);
        reg.Add<ComponentB>(newEid, compB2);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(newEid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        
        EXPECT_EQ(actualA.a, 200);
        EXPECT_EQ(actualB.x, 1.0f);
    }

    /// @brief Test GetHandle stress test with many entities.
    TEST(GetHandle, StressTest_ManyEntities_AllCorrect)
    {
        EcsRegistry reg;

        constexpr int NUM_ENTITIES = 1000;
        std::vector<EntityId> eids;
        std::unordered_map<EntityId, std::tuple<ComponentA, ComponentC>> components;

        for (int i = 0; i < NUM_ENTITIES; ++i)
        {
            auto eid = reg.Create();
            eids.push_back(eid);
            
            auto compA = ComponentA{ i };
            auto compC = ComponentC{ 
                static_cast<float>(i), 
                static_cast<float>(i * 2), 
                static_cast<float>(i * 3),
                std::to_string(i),
                i * 10
            };
            
            reg.Add<ComponentA>(eid, compA);
            reg.Add<ComponentC>(eid, compC);
            
            components.emplace(eid, std::make_tuple(compA, compC));
        }

        for (const auto& [eid, expected] : components)
        {
            auto handleResult = reg.GetHandle<ComponentA, ComponentC>(eid);
            EXPECT_TRUE(handleResult);

            auto handle = *handleResult;
            
            auto actualA = handle.Get<ComponentA>();
            auto actualC = handle.Get<ComponentC>();
            
            auto [expectedA, expectedC] = expected;
            
            EXPECT_EQ(actualA, expectedA);
            EXPECT_EQ(actualC, expectedC);
        }
    }

    /// @brief Test GetHandle with different component orderings - should work regardless of add order.
    TEST(GetHandle, DifferentComponentOrderings_WorksCorrectly)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        
        reg.Add<ComponentC>(eid, compC);
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        auto actualC = handle.Get<ComponentC>();
        
        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualB, compB);
        EXPECT_EQ(actualC, compC);
    }

    /// @brief Test GetHandle returns same EntityId as requested.
    TEST(GetHandle, GetEntityId_ReturnsSameEntityId)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        EXPECT_EQ(handle.GetEntityId(), eid);
    }

    /// @brief Test GetHandle can be copied and used independently.
    TEST(GetHandle, CopyHandle_BothAccessSameComponents)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = ComponentA{ 42 };
        
        reg.Add<ComponentA>(eid, compA);

        auto handleResult = reg.GetHandle<ComponentA>(eid);
        EXPECT_TRUE(handleResult);

        auto handle1 = *handleResult;
        auto handle2 = handle1;

        auto& comp1 = handle1.Get<ComponentA>();
        auto& comp2 = handle2.Get<ComponentA>();
        
        EXPECT_EQ(comp1, comp2); // Should point to same component
        EXPECT_EQ(comp1.a, 42);
        EXPECT_EQ(comp2.a, 42);

        // Modify through one handle
        comp1.a = 100;
        EXPECT_EQ(comp2.a, 100); // Should reflect in other handle
    }

    /// @brief Test GetHandle after component modification via GetComponents.
    TEST(GetHandle, AfterModificationViaGetComponents_ReturnsUpdatedValues)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = ComponentA{ 10 };
        auto compB = ComponentB{ 1.0f, 2.0f, 3.0f };
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        // Modify via GetHandle
        reg.GetHandle<ComponentA>(eid)->Get<ComponentA>().a = 50;
        reg.GetHandle<ComponentB>(eid)->Get<ComponentB>().x = 5.0f;

        // Get handle and verify it sees the modifications
        auto handleResult = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_TRUE(handleResult);

        auto handle = *handleResult;
        
        auto actualA = handle.Get<ComponentA>();
        auto actualB = handle.Get<ComponentB>();
        
        EXPECT_EQ(actualA.a, 50);
        EXPECT_EQ(actualB.x, 5.0f);
    }

    /// @brief Test multiple handles to same entity.
    TEST(GetHandle, MultipleHandlesSameEntity_AllAccessCorrectly)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = ComponentA{ 10 };
        auto compB = ComponentB{ 1.0f, 2.0f, 3.0f };
        auto compC = RandomValue<ComponentC>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        // Get different handles for different component combinations
        auto handleAB = *reg.GetHandle<ComponentA, ComponentB>(eid);
        auto handleAC = *reg.GetHandle<ComponentA, ComponentC>(eid);
        auto handleBC = *reg.GetHandle<ComponentB, ComponentC>(eid);

        // All handles should access the same components
        EXPECT_EQ(handleAB.Get<ComponentA>().a, 10);
        EXPECT_EQ(handleAC.Get<ComponentA>().a, 10);
        
        EXPECT_EQ(handleAB.Get<ComponentB>().x, 1.0f);
        EXPECT_EQ(handleBC.Get<ComponentB>().x, 1.0f);
        
        EXPECT_EQ(handleAC.Get<ComponentC>(), handleBC.Get<ComponentC>());
    }

    /// @brief Test GetHandle fails after component removal.
    TEST(GetHandle, AfterComponentRemoval_GetHandleFails)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        
        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        auto handleResult1 = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_TRUE(handleResult1);

        reg.Remove<ComponentB>(eid);

        // GetHandle should now fail since ComponentB was removed
        auto handleResult2 = reg.GetHandle<ComponentA, ComponentB>(eid);
        EXPECT_FALSE(handleResult2);
        EXPECT_EQ(handleResult2.error().Message, std::format("Entity {} does not have all requested components", eid));
    }
}
