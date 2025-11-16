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
    TEST(EntityId, Construct_Default_InvalidValue)
    {
        EntityId eid;

        //Default value of EntityId is InvalidValue
        EXPECT_FALSE(eid.IsValid());
        EXPECT_EQ((EntityId::ValueType)eid, EntityId::InvalidValue);
    }

    /// @brief Confirm an EntityId constructed with a value is valid and
    /// contains the correct value.
    TEST(EntityId, Construct_WithValue_ValidValue)
    {
        EcsRegistry reg;
        
        const auto eid1 = reg.Create();

        //Ensure eid correctly initialized.
        EXPECT_TRUE(eid1.IsValid());
        EXPECT_NE((EntityId::ValueType)eid1, EntityId::InvalidValue);

        //Ensure eid2 initialized from eid
        EntityId eid2 = eid1;
        EXPECT_TRUE(eid2.IsValid());
        EXPECT_EQ(eid1, eid2);
        EXPECT_EQ((EntityId::ValueType)eid2, (EntityId::ValueType)eid1);

        //Ensure assignment
        EntityId eid3;
        eid3 = eid1;
        EXPECT_TRUE(eid3.IsValid());
        EXPECT_EQ(eid1, eid3);
        EXPECT_EQ((EntityId::ValueType)eid3, (EntityId::ValueType)eid1);
    }

    /// @brief Test EntityId comparison operator for sorting.
    TEST(EntityId, Comparison_LessThan_SortsCorrectly)
    {
        EcsRegistry reg;

        const auto eid1 = reg.Create();
        const auto eid2 = reg.Create();
        const auto eid3 = reg.Create();

        //With a new instance of EcsRegistry, eid1 < eid2 < eid3, assuming no eids have been destroyed.

        EXPECT_TRUE(eid1 < eid2);
        EXPECT_TRUE(eid1 < eid3);
        EXPECT_TRUE(eid2 < eid3);
        EXPECT_TRUE(eid1 < eid3);
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

    /// @brief Test EntityId formatting with std::format.
    TEST(EntityId, Formatting_Format_ProducesCorrectString)
    {
        EcsRegistry reg;
        const auto eid = reg.Create();
        std::string formatted = std::format("{}", eid);
        EXPECT_EQ(formatted, std::to_string((EntityId::ValueType)eid));

        EntityId invalidEid;
        std::string formattedInvalid = std::format("{}", invalidEid);
        EXPECT_EQ(formattedInvalid, std::to_string(EntityId::InvalidValue));
    }

    /// @brief Test EntityId hashing for use in unordered containers.
    TEST(EntityId, Hashing_Hash_WorksInUnorderedMap)
    {
        EcsRegistry reg;

        const auto eid1 = reg.Create();
        const auto eid2 = reg.Create();
        const EntityId eid3 = eid1;  // same value as eid1

        std::unordered_map<EntityId, int> map;

        map[eid1] = 1;
        map[eid2] = 2;
        map[eid3] = 10;  // should overwrite eid1's value

        EXPECT_EQ(map.size(), 2);
        EXPECT_EQ(map[eid1], 10);
        EXPECT_EQ(map[eid2], 2);
    }

    /// @brief Test EntityId hashing in unordered_set.
    TEST(EntityId, Hashing_Hash_WorksInUnorderedSet)
    {
        EcsRegistry reg;

        const auto eid1 = reg.Create();
        const auto eid2 = reg.Create();
        const EntityId eid3 = eid1;  // same value as eid1

        std::unordered_set<EntityId> set;

        set.insert(eid1);
        set.insert(eid2);
        set.insert(eid3);

        EXPECT_EQ(set.size(), 2);
        EXPECT_TRUE(set.contains(eid1));
        EXPECT_TRUE(set.contains(eid2));
    }

    /// @brief Test EntityId with InvalidValue in comparisons.
    TEST(EntityId, InvalidValue_Comparison_BehavesCorrectly)
    {
        EcsRegistry reg;
        const EntityId invalid;
        const EntityId valid = reg.Create();

        // InvalidValue should be largest value
        EXPECT_TRUE(valid < invalid);
        EXPECT_FALSE(invalid < valid);
    }

    // ==================== EcsComponent Tests ====================

    /// @brief Test EcsComponent validity via bool conversion.
    TEST(EcsComponent, Validity_OperatorBool_IndicatesValidity)
    {
        ComponentA compA{ 42 };
        EcsComponent<ComponentA> validComponent(compA);
        EcsComponent<ComponentA> invalidComponent;

        EXPECT_TRUE(validComponent);
        EXPECT_FALSE(invalidComponent);
    }

    /// @brief Test EcsComponent NOT operator.
    TEST(EcsComponent, Validity_OperatorNot_IndicatesInvalidity)
    {
        ComponentA compA{ 42 };
        EcsComponent<ComponentA> validComponent(compA);
        EcsComponent<ComponentA> invalidComponent;

        EXPECT_FALSE(!validComponent);
        EXPECT_TRUE(!invalidComponent);
    }

    /// @brief Test EcsComponent value assignment modifies underlying value.
    TEST(EcsComponent, Assignment_ValueAssignment_ModifiesUnderlying)
    {
        ComponentA compA{ 42 };
        EcsComponent<ComponentA> component(compA);

        EXPECT_EQ(*component, ComponentA{ 42 });

        ComponentA newValue{ 100 };
        component = newValue;

        EXPECT_EQ(*component, newValue);
        EXPECT_EQ(compA.a, 100);  // original should be modified
    }

    /// @brief Test EcsComponent-to-EcsComponent assignment.
    TEST(EcsComponent, Assignment_ComponentToComponent_CopiesValue)
    {
        ComponentA compA{ 42 };
        ComponentA compB{ 100 };
        EcsComponent<ComponentA> componentA(compA);
        EcsComponent<ComponentA> componentB(compB);

        componentA = componentB;

        EXPECT_EQ(*componentA, ComponentA{ 100 });
        EXPECT_EQ(compA.a, 100);
    }

    /// @brief Test assignment to invalid component is safe (no-op).
    /// Invalid component remains invalid after assignment becuause invalid components
    /// do not have an underlying reference to modify.
    TEST(EcsComponent, Assignment_ToInvalidComponent_SafeNoOp)
    {
        ComponentA compA{ 42 };
        EcsComponent<ComponentA> validComponent(compA);
        EcsComponent<ComponentA> invalidComponent;

        // Should not throw or assert, just be a no-op
        invalidComponent = validComponent;

        EXPECT_EQ(*validComponent, ComponentA{ 42 });
        EXPECT_FALSE(invalidComponent);
    }

    /// @brief Test EcsComponent dereference on invalid component.
    TEST(EcsComponent, Dereference_OnInvalidComponent_AssertsOnDebug)
    {
        EcsComponent<ComponentA> invalidComponent;

        // This should trigger an assertion in debug builds
        // For this test, we just verify the component is invalid
        EXPECT_FALSE(invalidComponent);
    }

    /// @brief Test EcsComponent equality: EcsComponent == EcsComponent.
    TEST(EcsComponent, Equality_ComponentToComponent_CorrectComparison)
    {
        ComponentA compA{ 42 };
        ComponentA compB{ 42 };
        ComponentA compC{ 100 };

        EcsComponent<ComponentA> componentA(compA);
        EcsComponent<ComponentA> componentB(compB);
        EcsComponent<ComponentA> componentC(compC);
        EcsComponent<ComponentA> invalidComponent;

        EXPECT_EQ(componentA, componentB);
        EXPECT_NE(componentA, componentC);
        EXPECT_NE(componentA, invalidComponent);
        EXPECT_NE(invalidComponent, componentA);
    }

    /// @brief Test EcsComponent equality: EcsComponent == C.
    TEST(EcsComponent, Equality_ComponentToValue_CorrectComparison)
    {
        ComponentA compA{ 42 };
        ComponentA compB{ 42 };
        ComponentA compC{ 100 };

        EcsComponent<ComponentA> component(compA);
        EcsComponent<ComponentA> invalidComponent;

        EXPECT_EQ(component, compB);
        EXPECT_NE(component, compC);
        EXPECT_NE(invalidComponent, compB);
    }

    /// @brief Test EcsComponent equality: C == EcsComponent.
    TEST(EcsComponent, Equality_ValueToComponent_CorrectComparison)
    {
        ComponentA compA{ 42 };
        ComponentA compB{ 42 };
        ComponentA compC{ 100 };

        EcsComponent<ComponentA> component(compA);
        EcsComponent<ComponentA> invalidComponent;

        EXPECT_EQ(compB, component);
        EXPECT_NE(compC, component);
        EXPECT_NE(compB, invalidComponent);
    }

    /// @brief Test EcsComponent inequality operators.
    TEST(EcsComponent, Inequality_AllForms_CorrectComparison)
    {
        ComponentA compA{ 42 };
        ComponentA compC{ 100 };

        EcsComponent<ComponentA> component(compA);
        EcsComponent<ComponentA> otherComponent(compC);
        EcsComponent<ComponentA> invalidComponent;

        EXPECT_NE(component, otherComponent);
        EXPECT_NE(component, compC);
        EXPECT_NE(compA, otherComponent);
        EXPECT_NE(component, invalidComponent);
    }

    /// @brief Test EcsComponent self-assignment is safe.
    TEST(EcsComponent, Assignment_SelfAssignment_Safe)
    {
        ComponentA compA{ 42 };
        EcsComponent<ComponentA> component(compA);

        // This should not crash or cause issues
        component = *component;

        EXPECT_EQ(*component, ComponentA{ 42 });
    }

    // ==================== EcsComponentPool Tests ====================

/// @brief Test pool capacity management with many adds.
    TEST(EcsComponentPool, Capacity_ManyAdds_HandlesResizing)
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

        for (int i = 0; i < COUNT; ++i)
        {
            auto comp = pool.Get(eids[i]);
            EXPECT_TRUE(comp);
            EXPECT_EQ((*comp).a, i);
        }
    }

    /// @brief Test removing and re-adding to same entity.
    TEST(EcsComponentPool, AddRemoveAdd_Succeeds)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        ComponentA compA{ 42 };
        ComponentA compB{ 100 };

        // Add
        auto comp = pool.Add(eid, compA);
        EXPECT_TRUE(comp);
        EXPECT_EQ(*comp, compA);

        // Remove
        pool.Remove(eid);
        EXPECT_FALSE(pool.Has(eid));

        // Re-add
        comp = pool.Add(eid, compB);
        EXPECT_TRUE(comp);
        EXPECT_EQ(*comp, compB);
    }

    /// @brief Test remove correctly maintains association of component with entity ID.
    TEST(EcsComponentPool, Remove_MaintainsCorrectDataForEntity)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        // Add 5 entities
        std::vector<EntityId> eids;
        for (int i = 0; i < 5; ++i)
        {
            const auto eid = reg.Create();
            eids.push_back(eid);
            pool.Add(eid, ComponentA{ i * 10 });
        }

        // Remove entity at index 2
        pool.Remove(eids[2]);

        // Remaining entities should still have correct values
        EXPECT_FALSE(pool.Has(eids[2]));
        for (int i = 0; i < 5; ++i)
        {
            if (i != 2)
            {
                auto comp = pool.Get(eids[i]);
                EXPECT_TRUE(comp);
                EXPECT_EQ((*comp).a, i * 10);
            }
        }
    }

    /// @brief Test index consistency after mass add/remove operations.
    TEST(EcsComponentPool, IndexConsistency_MassOperations_RemainConsistent)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentC> pool;

        std::vector<EntityId> eids;
        constexpr int COUNT = 1000;

        // Add many
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

        // Verify remaining entities
        for (int i = 1; i < COUNT; i += 2)
        {
            auto comp = pool.Get(eids[i]);
            EXPECT_TRUE(comp);
            EXPECT_EQ((*comp).n, i);
        }

        // Verify removed entities
        for (int i = 0; i < COUNT; i += 2)
        {
            EXPECT_FALSE(pool.Has(eids[i]));
        }
    }

    /// @brief Test Has() with boundary EntityIds.
    TEST(EcsComponentPool, Has_BoundaryEntityIds_HandledCorrectly)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid1 = reg.Create(); // Should be 0
        const auto eid2 = reg.Create(); // Should be 1
        const auto eid3 = reg.Create(); // Should be 2

        EXPECT_EQ((EntityId::ValueType)eid1, 0);
        EXPECT_EQ((EntityId::ValueType)eid2, 1);
        EXPECT_EQ((EntityId::ValueType)eid3, 2);

        pool.Add(eid1, ComponentA{ 1 });
        pool.Add(eid3, ComponentA{ 3 });

        EXPECT_TRUE(pool.Has(eid1));
        EXPECT_FALSE(pool.Has(eid2));
        EXPECT_TRUE(pool.Has(eid3));
    }

    /// @brief Test Get on non-existent entity.
    TEST(EcsComponentPool, Get_NonExistentEntity_ReturnsInvalid)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();

        auto comp = pool.Get(eid);

        EXPECT_FALSE(comp);
    }

    /// @brief Test const Get method.
    TEST(EcsComponentPool, Get_ConstVersion_ReturnsConstComponent)
    {
        EcsRegistry reg;
        EcsComponentPool<ComponentA> pool;

        const auto eid = reg.Create();
        pool.Add(eid, ComponentA{ 42 });

        const EcsComponentPool<ComponentA>& constPool = pool;
        auto comp = constPool.Get(eid);

        EXPECT_TRUE(comp);
        EXPECT_EQ(*comp, ComponentA{ 42 });
    }
    
    // ==================== EcsRegistry Tests ====================

    /// @brief Confirm when a registry creates a new entity ID that it is valid and alive.
    TEST(EcsRegistry, Create_NewEntityAlive)
    {
        constexpr int NUM_TO_CREATE = 10;

        EcsRegistry reg;

        //Create a bunch of eids
        std::vector<EntityId> eids(NUM_TO_CREATE);
        for (auto& eid : eids)
        {
            eid = reg.Create();
            EXPECT_TRUE(eid.IsValid());
        }

        //Expect all eids to be unique
        std::set<EntityId> unique(eids.begin(), eids.end());
        EXPECT_EQ(unique.size(), eids.size());

        //Expect all eids to be alive
        for (auto eid : eids)
        {
            EXPECT_TRUE(reg.IsAlive(eid));
        }
    }

    /// @brief Confirm when a registry destroys an entity ID that it is no longer alive.
    TEST(EcsRegistry, Destroy_EntityNotAlive)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids = CreateEntityIds(reg);

        //Destroy them
        for (auto eid : eids)
        {
            reg.Destroy(eid);
        }

        //Expect no eids to be alive
        for (auto eid : eids)
        {
            EXPECT_FALSE(reg.IsAlive(eid));
        }
    }

    /// @brief COnfirm when entity IDs are destroyed and new ones created that
    /// the destroyed IDs are recycled.
    TEST(EcsRegistry, CreateDelete_EntityIdRecycled)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids1 = CreateEntityIds(reg);

        //Destroy them
        for (auto eid : eids1)
        {
            reg.Destroy(eid);
        }

        //Create a bunch of new eids
        auto eids2 = CreateEntityIds(reg);

        std::set<EntityId> unique1(eids1.begin(), eids1.end());
        std::set<EntityId> unique2(eids2.begin(), eids2.end());

        //All entity IDs should have been recycled.
        EXPECT_EQ(unique1, unique2);
    }

    /// @brief Confirm getting a component for an entity that does not have that
    /// component returns an invalid component.
    TEST(EcsRegistry, GetComponent_NoComponent_ReturnsInvalidComponent)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto component = reg.Get<ComponentA>(eid);

        EXPECT_FALSE(component);
    }

    /// @brief Confirm adding components to entities works and the correct
    /// components are returned.
    TEST(EcsRegistry, AddComponent_GetComponent_CorrectComponentReturned)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids = CreateEntityIds(reg);

        //Add components
        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            auto result = reg.Add<ComponentC>(eid, c);
            EXPECT_TRUE(result);
        }

        //Confirm components are correctly associated with entities.
        for (auto eid : eids)
        {
            //Component should be present.
            EXPECT_TRUE(reg.Has<ComponentC>(eid));

            const auto& expected = Cs[eid];
            const auto& actual = reg.Get<ComponentC>(eid);

            //Component should have the expected value.
            EXPECT_EQ(actual, expected);
        }
    }

    /// @brief Confirm adding a component to an entity that already has that component fails.
    TEST(EcsRegistry, AddComponent_DuplicateAdd_Fails)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids = CreateEntityIds(reg);

        //Add components
        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            reg.Add<ComponentC>(eid, c);
        }

        //Re-add components
        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            auto result = reg.Add<ComponentC>(eid, c);
            EXPECT_FALSE(result);
        }
    }

    /// @brief Confirm replacing components through the reference returned by Get works.
    TEST(EcsRegistry, AddComponent_ReplaceComponents_CorrectComponentReturned)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids = CreateEntityIds(reg);

        //Add components
        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            reg.Add<ComponentC>(eid, c);
        }

        //Replace components
        std::unordered_map<EntityId, ComponentC> newCs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            newCs.emplace(eid, c);
            *reg.Get<ComponentC>(eid) = c;
        }

        //Confirm components are correctly associated with entities.
        for (auto eid : eids)
        {
            //Component should be present.
            EXPECT_TRUE(reg.Has<ComponentC>(eid));

            const auto& expected = newCs[eid];
            const auto& actual = reg.Get<ComponentC>(eid);

            EXPECT_TRUE(actual);

            //Component should have the expected value.
            EXPECT_EQ(actual, expected);
        }
    }

    /// @brief Confirm an entity ID with associated components that is recycled
    /// no longer has components.
    TEST(EcsRegistry, AddComponents_RecycleEntityId_NoComponentsForEntity)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids = CreateEntityIds(reg);

        //Add components
        std::unordered_map<EntityId, ComponentC> Cs;

        for (auto eid : eids)
        {
            auto c = RandomValue<ComponentC>();
            Cs.emplace(eid, c);
            reg.Add<ComponentC>(eid, c);
        }

        //Destroy one of the entities
        const auto eidToRecycle = eids[eids.size() / 2];
        reg.Destroy(eidToRecycle);

        //Componenets should have been removed.
        EXPECT_FALSE(reg.Has<ComponentC>(eidToRecycle));

        //Create a new eid.
        const auto newEid = reg.Create();

        //Eid should have been recycled.
        EXPECT_EQ(eidToRecycle, newEid);

        //Confirm new eid has no components.
        EXPECT_FALSE(reg.Has<ComponentC>(eidToRecycle));

        EXPECT_FALSE(reg.Get<ComponentC>(eidToRecycle));

        auto newC = RandomValue<ComponentC>();
        reg.Add<ComponentC>(newEid, newC);

        //Confirm new eid has components.
        EXPECT_TRUE(reg.Has<ComponentC>(eidToRecycle));

        EXPECT_EQ(reg.Get<ComponentC>(newEid), newC);
    }

    /// @brief Confirm viewing entities with multiple components works correctly.
    TEST(EcsRegistry, GetView_CorrectComponentsReturned)
    {
        EcsRegistry reg;

        //Create a bunch of eids        
        auto eids = CreateEntityIds(reg);

        std::unordered_map<EntityId, std::tuple<ComponentA, ComponentB, ComponentC>> components;

        //Add components
        for (auto eid : eids)
        {
            auto a = RandomValue<ComponentA>();
            auto b = RandomValue<ComponentB>();
            auto c = RandomValue<ComponentC>();
            components.emplace(eid, std::make_tuple(a, b, c));
            reg.Add<ComponentA>(eid, a);
            reg.Add<ComponentB>(eid, b);
            reg.Add<ComponentC>(eid, c);
        }
        
        //Check views
        for(auto eid : eids)
        {
            auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
            EXPECT_TRUE(view);

            auto [expectedA, expectedB, expectedC] = components[eid];
            auto [actualA, actualB, actualC] = *view;

            EXPECT_EQ(expectedA, actualA);
            EXPECT_EQ(expectedB, actualB);
            EXPECT_EQ(expectedC, actualC);
        }
    }

    /// @brief Confirm that requesting a view for an entity that is not alive returns an error.
    TEST(EcsRegistry, GetView_EntityNotAlive_ErrorReturned)
    {
        EcsRegistry reg;

        const auto eid = reg.Create();
        reg.Destroy(eid);

        auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(view);

        EXPECT_EQ(view.error().Message, std::format("Entity {} is not alive", eid));
    }

    /// @brief Confirm that requesting a view for an entity with no components returns an error.
    TEST(EcsRegistry, GetView_NoComponents_ErrorReturned)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(view);

        EXPECT_EQ(view.error().Message, std::format("Entity {} does not have all requested components:  {} {} {}", eid, typeid(ComponentA).name(), typeid(ComponentB).name(), typeid(ComponentC).name()));
    }

    /// @brief Confirm that requesting a view for an entity missing components returns an error.
    TEST(EcsRegistry, GetView_MissingComponents_ErrorReturned)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());

        auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(view);

        EXPECT_EQ(view.error().Message, std::format("Entity {} does not have all requested components:  {} {}", eid, typeid(ComponentB).name(), typeid(ComponentC).name()));
    }/// @brief Test adding multiple different component types to same entity.
    TEST(EcsRegistry, AddComponents_AddDifferentTypes_AllAccessible)
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

        EXPECT_EQ(reg.Get<ComponentA>(eid), compA);
        EXPECT_EQ(reg.Get<ComponentB>(eid), compB);
        EXPECT_EQ(reg.Get<ComponentC>(eid), compC);
        EXPECT_EQ(reg.Get<ComponentD>(eid), compD);
    }

    /// @brief Test that destroying entity removes all component types.
    TEST(EcsRegistry, AddComponents_DestroyEntity_AllRemoved)
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

    /// @brief Test component access after entity destruction.
    TEST(EcsRegistry, ComponentAccess_AfterDestroy_ReturnsInvalid)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        reg.Add<ComponentA>(eid, ComponentA{ 42 });

        EXPECT_TRUE(reg.Has<ComponentA>(eid));

        reg.Destroy(eid);

        EXPECT_FALSE(reg.Has<ComponentA>(eid));
        auto comp = reg.Get<ComponentA>(eid);
        EXPECT_FALSE(comp);
    }

    /// @brief Test that IsAlive returns false for non-existent entities.
    TEST(EcsRegistry, IsAlive_NonExistentEntity_ReturnsFalse)
    {
        EcsRegistry reg;
        EntityId eid;

        EXPECT_FALSE(reg.IsAlive(eid));
    }

    /// @brief Test mixed create/destroy/recycle patterns.
    TEST(EcsRegistry, MixedPatterns_ComplexCreateDestroySequence_Consistent)
    {
        EcsRegistry reg;

        std::vector<EntityId> alive;

        // Create some
        for (int i = 0; i < 10; ++i)
        {
            alive.push_back(reg.Create());
        }

        // Destroy some
        reg.Destroy(alive[2]);
        reg.Destroy(alive[5]);
        alive.erase(alive.begin() + 5);
        alive.erase(alive.begin() + 2);

        // Create new ones (should recycle)
        EntityId recycled1 = reg.Create();
        EntityId recycled2 = reg.Create();

        // Verify new ones are recycled IDs
        EXPECT_TRUE(reg.IsAlive(recycled1));
        EXPECT_TRUE(reg.IsAlive(recycled2));

        // Verify all alive entities are still alive
        for (auto eid : alive)
        {
            EXPECT_TRUE(reg.IsAlive(eid));
        }
    }

    /// @brief Test GetView with subset of components (entity has more than requested).
    TEST(EcsRegistry, GetView_Subset_SucceedsWithRelevantComponents)
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

        // Request view for subset
        auto viewResult = reg.GetView<ComponentA, ComponentC>(eid);
        EXPECT_TRUE(viewResult);

        auto& view = *viewResult;
        auto [actualA, actualC] = view;

        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualC, compC);
    }

    /// @brief Test adding components after partial view failure.
    TEST(EcsRegistry, GetView_AddMissingComponents_ViewSucceedsAfter)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        reg.Add<ComponentA>(eid, compA);

        // Should fail - missing B and C
        auto viewResult1 = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(viewResult1);

        // Add missing components
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        // Should now succeed
        auto viewResult2 = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(viewResult2);

        auto& view = *viewResult2;
        auto [actualA, actualB, actualC] = view;

        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualB, compB);
        EXPECT_EQ(actualC, compC);
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
            (reg.Add<Cs>(eid, RandomValue<Cs>()), ...);
            auto view = reg.GetView<Cs...>(eid);
            components.emplace(eid, std::make_tuple((*view).get<Cs>()...));
        }

        return eids;
    }

    /// @brief Helper to verify that filtered views return the expected components.
    template<typename... Cs>
    static void VerifyViewComponents(
        EcsRegistry& reg,
        std::unordered_map<EntityId, std::tuple<Cs...>>& expectedComponents)
    {
        for (auto& [eid, expectedTuple] : expectedComponents)
        {
            auto view = reg.GetView<Cs...>(eid);
            EXPECT_TRUE(view);

            auto actualTuple = std::make_tuple((*view).get<Cs>()...);

            EXPECT_EQ(actualTuple, expectedTuple);
        }
    }

    /// @brief Confirm viewing entities with multiple components works correctly.
    TEST(EcsRegistry, FilteredView_Iteration_CorrectComponentsReturned)
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

        //Verify filtered views
        VerifyViewComponents(reg, componentsA);
        VerifyViewComponents(reg, componentsB);
        VerifyViewComponents(reg, componentsC);
        VerifyViewComponents(reg, componentsAB);
        VerifyViewComponents(reg, componentsAC);
        VerifyViewComponents(reg, componentsBC);
        VerifyViewComponents(reg, componentsABC);
    }

    // ==================== EcsView Tests ====================

    /// @brief Test EcsView element access via index.
    TEST(EcsView, ElementAccess_ByIndex_CorrectRetrieval)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();

        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        auto viewResult = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(viewResult);

        auto& view = *viewResult;

        // Access by index
        EXPECT_EQ(view.get<0>(), compA);
        EXPECT_EQ(view.get<1>(), compB);
        EXPECT_EQ(view.get<2>(), compC);
    }

    /// @brief Test EcsView element access via type.
    TEST(EcsView, ElementAccess_ByType_CorrectRetrieval)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();

        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        auto viewResult = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(viewResult);

        auto& view = *viewResult;

        // Access by type
        EXPECT_EQ(view.get<ComponentA>(), compA);
        EXPECT_EQ(view.get<ComponentB>(), compB);
        EXPECT_EQ(view.get<ComponentC>(), compC);
    }

    /// @brief Test EcsView const correctness.
    TEST(EcsView, ConstCorrectness_ConstView_ReturnsConstReferences)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();

        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);

        const auto viewResult = reg.GetView<ComponentA, ComponentB>(eid);
        EXPECT_TRUE(viewResult);

        const auto& view = *viewResult;

        // These should be const references
        const auto& a = view.get<0>();
        const auto& b = view.get<1>();

        EXPECT_EQ(a, compA);
        EXPECT_EQ(b, compB);
    }

    /// @brief Test EcsView with single component.
    TEST(EcsView, SingleComponent_View_WorksCorrectly)
    {
        EcsRegistry reg;

        auto eid = reg.Create();
        auto compA = RandomValue<ComponentA>();

        reg.Add<ComponentA>(eid, compA);

        auto viewResult = reg.GetView<ComponentA>(eid);
        EXPECT_TRUE(viewResult);

        auto& view = *viewResult;

        EXPECT_EQ(view.get<0>(), compA);
        EXPECT_EQ(view.get<ComponentA>(), compA);
    }

    /// @brief Test structured bindings with EcsView.
    TEST(EcsView, StructuredBindings_AutoDecomposition_CorrectValues)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto compA = RandomValue<ComponentA>();
        auto compB = RandomValue<ComponentB>();
        auto compC = RandomValue<ComponentC>();

        reg.Add<ComponentA>(eid, compA);
        reg.Add<ComponentB>(eid, compB);
        reg.Add<ComponentC>(eid, compC);

        auto viewResult = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_TRUE(viewResult);

        auto& view = *viewResult;

        // Use structured bindings
        auto [actualA, actualB, actualC] = view;

        EXPECT_EQ(actualA, compA);
        EXPECT_EQ(actualB, compB);
        EXPECT_EQ(actualC, compC);
    }

    // ==================== FilteredView Tests ====================

    /// @brief Test FilteredView with empty results.
    TEST(FilteredView, EmptyResults_NoEntityMatches_IterationEmpty)
    {
        EcsRegistry reg;

        // Create entities with ComponentA only
        for (int i = 0; i < 5; ++i)
        {
            auto eid = reg.Create();
            reg.Add<ComponentA>(eid, RandomValue<ComponentA>());
        }

        // Filter for ComponentB should return nothing
        int count = 0;
        for (auto view : reg.Filter<ComponentB>())
        {
            count++;
        }

        EXPECT_EQ(count, 0);
    }

    /// @brief Test FilteredView with full results.
    TEST(FilteredView, FullResults_AllEntitiesMatch_IterationFull)
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

        // Filter for ComponentA should return all
        int count = 0;
        std::vector<EntityId> filtered;
        for (auto view : reg.Filter<ComponentA>())
        {
            filtered.push_back(view.Eid);
            count++;
        }

        EXPECT_EQ(count, NUM_ENTITIES);
        std::sort(eids.begin(), eids.end());
        std::sort(filtered.begin(), filtered.end());
        EXPECT_EQ(eids, filtered);
    }

    /// @brief Test FilteredView with partial results.
    TEST(FilteredView, PartialResults_SomeEntitiesMatch_IterationPartial)
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

        // Filter for ComponentA and ComponentB should return 7
        int count = 0;
        std::vector<EntityId> filtered;
        for (auto view : reg.Filter<ComponentA, ComponentB>())
        {
            filtered.push_back(view.Eid);
            count++;
        }

        EXPECT_EQ(count, 7);
        std::sort(withAB.begin(), withAB.end());
        std::sort(filtered.begin(), filtered.end());
        EXPECT_EQ(withAB, filtered);
    }

    /// @brief Test multiple FilteredView iterations.
    TEST(FilteredView, MultipleIterations_CallFilterRepeatedly_ConsistentResults)
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
        for (auto view : reg.Filter<ComponentA, ComponentB>())
        {
            results1.push_back(view.Eid);
        }

        std::vector<EntityId> results2;
        for (auto view : reg.Filter<ComponentA, ComponentB>())
        {
            results2.push_back(view.Eid);
        }

        std::sort(results1.begin(), results1.end());
        std::sort(results2.begin(), results2.end());

        EXPECT_EQ(results1, results2);
    }

    /// @brief Test FilteredView with single component.
    TEST(FilteredView, SingleComponent_Filter_ReturnsCorrectEntities)
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

        // Filter for ComponentA
        int count = 0;
        std::vector<EntityId> filtered;
        for (auto view : reg.Filter<ComponentA>())
        {
            filtered.push_back(view.Eid);
            count++;
        }

        EXPECT_EQ(count, 5);
        std::sort(withA.begin(), withA.end());
        std::sort(filtered.begin(), filtered.end());
        EXPECT_EQ(withA, filtered);
    }

    /// @brief Test FilteredView consistency with manual checks.
    TEST(FilteredView, Consistency_FilterVsManualCheck_MatchResults)
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
        for (auto view : reg.Filter<ComponentA>())
        {
            allAlive.push_back(view.Eid);
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

        // Filter for A and B
        std::vector<EntityId> filtered;
        for (auto view : reg.Filter<ComponentA, ComponentB>())
        {
            filtered.push_back(view.Eid);
        }

        std::sort(manualCheck.begin(), manualCheck.end());
        std::sort(filtered.begin(), filtered.end());

        EXPECT_EQ(filtered, manualCheck);
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

        // Filter and verify
        int countA = 0, countAB = 0, countABC = 0;

        for (auto view : reg.Filter<ComponentA>()) { countA++; }
        for (auto view : reg.Filter<ComponentA, ComponentB>()) { countAB++; }
        for (auto view : reg.Filter<ComponentA, ComponentB, ComponentC>()) { countABC++; }

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
            auto compA = reg.Get<ComponentA>(eids[i]);
            EXPECT_TRUE(compA);
            EXPECT_EQ((*compA).a, i);

            auto compB = reg.Get<ComponentB>(eids[i]);
            EXPECT_TRUE(compB);
            EXPECT_EQ((*compB).x, static_cast<float>(i));

            auto compC = reg.Get<ComponentC>(eids[i]);
            EXPECT_TRUE(compC);

            auto compD = reg.Get<ComponentD>(eids[i]);
            EXPECT_TRUE(compD);
        }
    }
}