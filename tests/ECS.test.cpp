#include <gtest/gtest.h>

#include <random>
#include <array>

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

    constexpr int MAX_ENTITIES = 1000;

    /// @brief Use the registry to create a number of entity IDs.
    /// @param reg 
    /// @param count 
    /// @return entity IDs
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
    /// @param reg 
    /// @return entity IDs
    std::vector<EntityId> CreateEntityIds(EcsRegistry& reg)
    {
        return CreateEntityIds(reg, MAX_ENTITIES);
    }

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
        const EntityId::ValueType testValue = 1234;

        //Ensure eid correctly initialized.
        EntityId eid{ testValue };
        EXPECT_TRUE(eid.IsValid());
        EXPECT_EQ((EntityId::ValueType)eid, testValue);

        //Ensure eid2 initialized from eid
        EntityId eid2 = eid;
        EXPECT_TRUE(eid2.IsValid());
        EXPECT_EQ(eid, eid2);
        EXPECT_EQ((EntityId::ValueType)eid2, testValue);

        //Ensure assignment
        EntityId eid3;
        eid3 = eid;
        EXPECT_TRUE(eid3.IsValid());
        EXPECT_EQ(eid, eid3);
        EXPECT_EQ((EntityId::ValueType)eid3, testValue);

        //Ensure conversion to integral value
        const uint64_t value = eid;
        EXPECT_EQ(value, testValue);
    }

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
    TEST(EcsRegistry, View_GetView_CorrectComponentsReturned)
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
    TEST(EcsRegistry, View_GetView_EntityNotAlive_ErrorReturned)
    {
        EcsRegistry reg;

        EntityId eid{1234};

        auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(view);

        EXPECT_EQ(view.error().Message, "Entity 1234 is not alive");
    }

    /// @brief Confirm that requesting a view for an entity with no components returns an error.
    TEST(EcsRegistry, View_GetView_NoComponents_ErrorReturned)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(view);

        EXPECT_EQ(view.error().Message, std::format("Entity {} does not have all requested components:  {} {} {}", eid, typeid(ComponentA).name(), typeid(ComponentB).name(), typeid(ComponentC).name()));
    }

    /// @brief Confirm that requesting a view for an entity missing components returns an error.
    TEST(EcsRegistry, View_GetView_MissingComponents_ErrorReturned)
    {
        EcsRegistry reg;

        auto eid = reg.Create();

        reg.Add<ComponentA>(eid, RandomValue<ComponentA>());

        auto view = reg.GetView<ComponentA, ComponentB, ComponentC>(eid);
        EXPECT_FALSE(view);

        EXPECT_EQ(view.error().Message, std::format("Entity {} does not have all requested components:  {} {}", eid, typeid(ComponentB).name(), typeid(ComponentC).name()));
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
            componentsA.emplace(comp.first,  std::make_tuple(std::get<0>(comp.second)));
            componentsC.emplace(comp.first,  std::make_tuple(std::get<1>(comp.second)));
        }
        for(auto comp : componentsBC)
        {
            componentsB.emplace(comp.first,  std::make_tuple(std::get<0>(comp.second)));
            componentsC.emplace(comp.first,  std::make_tuple(std::get<1>(comp.second)));
        }
        for(auto comp : componentsABC)
        {
            componentsA.emplace(comp.first,  std::make_tuple(std::get<0>(comp.second)));
            componentsB.emplace(comp.first,  std::make_tuple(std::get<1>(comp.second)));
            componentsC.emplace(comp.first,  std::make_tuple(std::get<2>(comp.second)));
            componentsAB.emplace(comp.first,  std::make_tuple(std::get<0>(comp.second), std::get<1>(comp.second)));
            componentsAC.emplace(comp.first,  std::make_tuple(std::get<0>(comp.second), std::get<2>(comp.second)));
            componentsBC.emplace(comp.first,  std::make_tuple(std::get<1>(comp.second), std::get<2>(comp.second)));
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
}