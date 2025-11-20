#include <gtest/gtest.h>
#include "EcsTransformNodePool.h"

#include <algorithm>
#include <random>
#include <ctime>
#include <iostream>

namespace
{
    /// @brief Helper to create a simple EntityId for testing.
    class TestIdGenerator
    {
    public:
        EntityId NextId()
        {
            return m_Registry.Create();
        }

        EcsRegistry& Registry()
        {
            return m_Registry;
        }

    private:
        EcsRegistry m_Registry;
    };

    // ========== Basic Operations Tests ==========

    /// @brief Verifies that a single top-level part can be added and retrieved correctly.
    TEST(AssemblyCollection, Add_SingleTopLevelPart_PartAddedSuccessfully)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId eid = idGen.NextId();
        collection.Add(eid, Part{ .Id = eid });

        EXPECT_EQ(collection.size(), 1);
        EXPECT_TRUE(collection.Has(eid));
        
        const Part* part = collection.Get(eid);
        ASSERT_NE(part, nullptr);
        EXPECT_EQ(part->Id, eid);
        EXPECT_FALSE(part->ParentId.IsValid());
    }

    /// @brief Verifies that multiple top-level parts can be added independently.
    TEST(AssemblyCollection, Add_MultipleTopLevelParts_AllPartsAddedSuccessfully)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId id1 = idGen.NextId();
        const EntityId id2 = idGen.NextId();
        const EntityId id3 = idGen.NextId();

        collection.Add(id1);
        collection.Add(id2);
        collection.Add(id3);

        EXPECT_EQ(collection.size(), 3);
        EXPECT_TRUE(collection.Has(id1));
        EXPECT_TRUE(collection.Has(id2));
        EXPECT_TRUE(collection.Has(id3));

        const Part* part1 = collection.Get(id1);
        const Part* part2 = collection.Get(id2);
        const Part* part3 = collection.Get(id3);

        EXPECT_NE(part1, nullptr);
        EXPECT_NE(part2, nullptr);
        EXPECT_NE(part3, nullptr);

        EXPECT_EQ(part1->Id, id1);
        EXPECT_EQ(part2->Id, id2);
        EXPECT_EQ(part3->Id, id3);

        EXPECT_FALSE(part1->ParentId.IsValid());
        EXPECT_FALSE(part2->ParentId.IsValid());
        EXPECT_FALSE(part3->ParentId.IsValid());
    }

    /// @brief Verifies that a child part is added after its parent and maintains correct parent-child relationship.
    TEST(AssemblyCollection, Add_SingleChildToParent_ChildAddedAfterParent)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId parentId = idGen.NextId();
        const EntityId childId = idGen.NextId();

        collection.Add(parentId);
        collection.Add(childId, Part{ .Id = childId, .ParentId = parentId });

        EXPECT_EQ(collection.size(), 2);
        EXPECT_TRUE(collection.Has(parentId));
        EXPECT_TRUE(collection.Has(childId));

        const Part* child = collection.Get(childId);
        ASSERT_NE(child, nullptr);
        EXPECT_EQ(child->ParentId, parentId);

        // Verify child appears after parent in iteration order
        auto it = collection.begin();
        EXPECT_EQ(it->Id, parentId);
        ++it;
        EXPECT_EQ(it->Id, childId);
    }

    /// @brief Verifies that multiple children are added consecutively after their parent with correct relationships.
    TEST(AssemblyCollection, Add_MultipleChildrenToParent_AllChildrenAddedConsecutively)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId parentId = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId child3 = idGen.NextId();

        collection.Add(parentId);
        collection.Add(child1, Part{ .Id = child1, .ParentId = parentId });
        collection.Add(child2, Part{ .Id = child2, .ParentId = parentId });
        collection.Add(child3, Part{ .Id = child3, .ParentId = parentId });

        EXPECT_EQ(collection.size(), 4);

        // Verify all children have correct parent
        EXPECT_EQ(collection.Get(child1)->ParentId, parentId);
        EXPECT_EQ(collection.Get(child2)->ParentId, parentId);
        EXPECT_EQ(collection.Get(child3)->ParentId, parentId);

        // Verify ordering: parent followed by all children, children in reverse order of addition
        auto it = collection.begin();
        EXPECT_EQ(it->Id, parentId);
        ++it;
        EXPECT_EQ(it->ParentId, parentId); // child3
        EXPECT_EQ(it->Id, child3);
        ++it;
        EXPECT_EQ(it->ParentId, parentId); // child2
        EXPECT_EQ(it->Id, child2);
        ++it;
        EXPECT_EQ(it->ParentId, parentId); // child1
        EXPECT_EQ(it->Id, child1);
    }

    /// @brief Verifies that attempting to add an invalid EntityId is rejected and collection remains unchanged.
    TEST(AssemblyCollection, Add_InvalidEntityId_AddRejected)
    {
        AssemblyCollection collection;
        
        EntityId invalidId; // Default constructor creates invalid ID
        EXPECT_FALSE(invalidId.IsValid());

        collection.Add(invalidId);

        EXPECT_EQ(collection.size(), 0);
        EXPECT_FALSE(collection.Has(invalidId));
    }

    /// @brief Verifies that attempting to add a duplicate EntityId is rejected.
    TEST(AssemblyCollection, Add_DuplicateEntityId_AddRejected)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId partId = idGen.NextId();
        
        collection.Add(partId);
        EXPECT_EQ(collection.size(), 1);

        // Attempt to add same ID again
        collection.Add(partId);
        EXPECT_EQ(collection.size(), 1); // Size should not change
    }

    /// @brief Verifies that attempting to add an entity with itself as parent is rejected.
    TEST(AssemblyCollection, Add_EntityWithSelfAsParent_AddRejected)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId partId = idGen.NextId();
        
        // Add as top-level first
        collection.Add(partId);
        const size_t initialSize = collection.size();

        // Attempt to add same ID with itself as parent
        const EntityId sameId = idGen.NextId();
        collection.Add(sameId, Part{ .Id = sameId, .ParentId = sameId });

        EXPECT_EQ(collection.size(), initialSize); // Size should not change
        EXPECT_FALSE(collection.Has(sameId));
    }

    /// @brief Verifies that attempting to add a child with a non-existent parent is rejected.
    TEST(AssemblyCollection, Add_ChildWithNonExistentParent_AddRejected)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId childId = idGen.NextId();
        const EntityId nonExistentParent = idGen.NextId();

        // Attempt to add child with parent that doesn't exist
        collection.Add(childId, Part{ .Id = childId, .ParentId = nonExistentParent });

        EXPECT_EQ(collection.size(), 0);
        EXPECT_FALSE(collection.Has(childId));
    }

    // ========== Hierarchical Structure Tests ==========

    /// @brief Verifies that a three-level nested hierarchy maintains correct ordering and relationships.
    TEST(AssemblyCollection, Add_ThreeLevelNestedHierarchy_CorrectOrderingMaintained)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId grandparent = idGen.NextId();
        const EntityId parent = idGen.NextId();
        const EntityId child = idGen.NextId();

        collection.Add(grandparent);
        collection.Add(parent, Part{ .Id = parent, .ParentId = grandparent });
        collection.Add(child, Part{ .Id = child, .ParentId = parent });

        EXPECT_EQ(collection.size(), 3);

        // Verify relationships
        EXPECT_FALSE(collection.Get(grandparent)->ParentId.IsValid());
        EXPECT_EQ(collection.Get(parent)->ParentId, grandparent);
        EXPECT_EQ(collection.Get(child)->ParentId, parent);

        // Verify ordering
        auto it = collection.begin();
        EXPECT_EQ(it->Id, grandparent);
        ++it;
        EXPECT_EQ(it->Id, parent);
        ++it;
        EXPECT_EQ(it->Id, child);
    }

    /// @brief Verifies that a hierarchy with multiple branches and grandchildren maintains proper structure.
    TEST(AssemblyCollection, Add_MultipleBranchesWithGrandchildren_ProperStructureMaintained)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId grandchild1_1 = idGen.NextId();
        const EntityId grandchild1_2 = idGen.NextId();
        const EntityId grandchild2_1 = idGen.NextId();

        collection.Add(root);
        collection.Add(child1, Part{ .Id = child1, .ParentId = root });
        collection.Add(grandchild1_1, Part{ .Id = grandchild1_1, .ParentId = child1 });
        collection.Add(grandchild1_2, Part{ .Id = grandchild1_2, .ParentId = child1 });
        collection.Add(child2, Part{ .Id = child2, .ParentId = root });
        collection.Add(grandchild2_1, Part{ .Id = grandchild2_1, .ParentId = child2 });
        EXPECT_EQ(collection.size(), 6);

        // Verify all parts exist
        EXPECT_TRUE(collection.Has(root));
        EXPECT_TRUE(collection.Has(child1));
        EXPECT_TRUE(collection.Has(child2));
        EXPECT_TRUE(collection.Has(grandchild1_1));
        EXPECT_TRUE(collection.Has(grandchild1_2));
        EXPECT_TRUE(collection.Has(grandchild2_1));

        // Verify relationships
        EXPECT_EQ(collection.Get(child1)->ParentId, root);
        EXPECT_EQ(collection.Get(child2)->ParentId, root);
        EXPECT_EQ(collection.Get(grandchild1_1)->ParentId, child1);
        EXPECT_EQ(collection.Get(grandchild1_2)->ParentId, child1);
        EXPECT_EQ(collection.Get(grandchild2_1)->ParentId, child2);

        // Verify depth first ordering
        auto it = collection.begin();
        EXPECT_EQ(it->Id, root);
        ++it;
        EXPECT_EQ(it->Id, child2);
        ++it;
        EXPECT_EQ(it->Id, grandchild2_1);
        ++it;
        EXPECT_EQ(it->Id, child1);
        ++it;
        EXPECT_EQ(it->Id, grandchild1_2);
        ++it;
        EXPECT_EQ(it->Id, grandchild1_1);
    }

    /// @brief Verifies that adding a child to a middle node correctly inserts it and updates indices.
    TEST(AssemblyCollection, Add_ChildToMiddleNode_InsertedCorrectlyWithUpdatedIndices)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // Build initial hierarchy
        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();

        collection.Add(root);
        collection.Add(child1, Part{ .Id = child1, .ParentId = root });
        collection.Add(child2, Part{ .Id = child2, .ParentId = root });

        EXPECT_EQ(collection.size(), 3);

        // Add a new child to the second child (middle of hierarchy)
        const EntityId grandchild = idGen.NextId();
        collection.Add(grandchild, Part{ .Id = grandchild, .ParentId = child2 });

        EXPECT_EQ(collection.size(), 4);
        EXPECT_TRUE(collection.Has(grandchild));
        EXPECT_EQ(collection.Get(grandchild)->ParentId, child2);

        // Verify ordering: root, child2, grandchild, child1
        auto it = collection.begin();
        EXPECT_EQ(it->Id, root);
        ++it;
        EXPECT_EQ(it->Id, child2);
        ++it;
        EXPECT_EQ(it->Id, grandchild);
        ++it;
        EXPECT_EQ(it->Id, child1);
    }

    // ========== Removal Tests ==========

    /// @brief Verifies that removing a top-level part without children completely removes it from the collection.
    TEST(AssemblyCollection, Remove_TopLevelPartWithoutChildren_PartRemovedCompletely)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId partId = idGen.NextId();
        collection.Add(partId);

        EXPECT_EQ(collection.size(), 1);
        EXPECT_TRUE(collection.Has(partId));

        collection.Remove(partId);

        EXPECT_EQ(collection.size(), 0);
        EXPECT_FALSE(collection.Has(partId));
        EXPECT_EQ(collection.Get(partId), nullptr);
    }

    /// @brief Verifies that removing a parent part also removes all its children (entire subtree).
    TEST(AssemblyCollection, Remove_ParentWithChildren_EntireSubtreeRemoved)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId parent = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();

        collection.Add(parent);
        collection.Add(child1, Part{ .Id = child1, .ParentId = parent });
        collection.Add(child2, Part{ .Id = child2, .ParentId = parent });

        EXPECT_EQ(collection.size(), 3);

        collection.Remove(parent);

        EXPECT_EQ(collection.size(), 0);
        EXPECT_FALSE(collection.Has(parent));
        EXPECT_FALSE(collection.Has(child1));
        EXPECT_FALSE(collection.Has(child2));
    }

    /// @brief Verifies that removing a middle child leaves its siblings intact.
    TEST(AssemblyCollection, Remove_MiddleChild_SiblingsRemainIntact)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId parent = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId child3 = idGen.NextId();

        collection.Add(parent);
        collection.Add(child1, Part{ .Id = child1, .ParentId = parent });
        collection.Add(child2, Part{ .Id = child2, .ParentId = parent });
        collection.Add(child3, Part{ .Id = child3, .ParentId = parent });

        EXPECT_EQ(collection.size(), 4);

        collection.Remove(child2);

        EXPECT_EQ(collection.size(), 3);
        EXPECT_TRUE(collection.Has(parent));
        EXPECT_TRUE(collection.Has(child1));
        EXPECT_FALSE(collection.Has(child2));
        EXPECT_TRUE(collection.Has(child3));

        //Verify ordering: parent, child3, child1
        auto it = collection.begin();
        EXPECT_EQ(it->Id, parent);
        ++it;
        EXPECT_EQ(it->Id, child3);
        ++it;
        EXPECT_EQ(it->Id, child1);
    }

    /// @brief Verifies that attempting to remove a non-existent entity has no effect on the collection.
    TEST(AssemblyCollection, Remove_NonExistentEntity_NoEffect)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId existingId = idGen.NextId();
        const EntityId nonExistentId = idGen.NextId();

        collection.Add(existingId);
        EXPECT_EQ(collection.size(), 1);

        collection.Remove(nonExistentId);

        EXPECT_EQ(collection.size(), 1);
        EXPECT_TRUE(collection.Has(existingId));
    }

    /// @brief Verifies that removing a node in a deep hierarchy removes all its descendants.
    TEST(AssemblyCollection, Remove_NodeInDeepHierarchy_AllDescendantsRemoved)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // Build 4-level hierarchy
        const EntityId level1 = idGen.NextId();
        const EntityId level2 = idGen.NextId();
        const EntityId level3 = idGen.NextId();
        const EntityId level4 = idGen.NextId();
        const EntityId level3_sibling = idGen.NextId();

        collection.Add(level1);
        collection.Add(level2, Part{ .Id = level2, .ParentId = level1 });
        collection.Add(level3, Part{ .Id = level3, .ParentId = level2 });
        collection.Add(level4, Part{ .Id = level4, .ParentId = level3 });
        collection.Add(level3_sibling, Part{ .Id = level3_sibling, .ParentId = level2 });

        EXPECT_EQ(collection.size(), 5);

        // Remove middle node (level2)
        collection.Remove(level2);

        EXPECT_EQ(collection.size(), 1); // Only level1 remains
        EXPECT_TRUE(collection.Has(level1));
        EXPECT_FALSE(collection.Has(level2));
        EXPECT_FALSE(collection.Has(level3));
        EXPECT_FALSE(collection.Has(level4));
        EXPECT_FALSE(collection.Has(level3_sibling));
    }

    /// @brief Verifies that an entity can be removed and re-added, both as top-level and as a child.
    TEST(AssemblyCollection, Remove_ThenReAdd_EntityAddedSuccessfully)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId partId = idGen.NextId();
        
        // Add, remove, then add again as top-level
        collection.Add(partId);
        EXPECT_TRUE(collection.Has(partId));

        collection.Remove(partId);
        EXPECT_FALSE(collection.Has(partId));

        collection.Add(partId);
        EXPECT_TRUE(collection.Has(partId));
        EXPECT_EQ(collection.size(), 1);

        // Now add as child
        const EntityId parentId = idGen.NextId();
        collection.Add(parentId);
        
        collection.Remove(partId);
        EXPECT_FALSE(collection.Has(partId));
        
        collection.Add(partId, Part{ .Id = partId, .ParentId = parentId });
        EXPECT_TRUE(collection.Has(partId));
        EXPECT_EQ(collection.Get(partId)->ParentId, parentId);
    }

    // ========== Query/Access Tests ==========

    /// @brief Verifies that Get() returns a valid pointer for an existing entity.
    TEST(AssemblyCollection, Get_ExistingEntity_ValidPointerReturned)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId partId = idGen.NextId();
        collection.Add(partId);

        Part* part = collection.Get(partId);
        ASSERT_NE(part, nullptr);
        EXPECT_EQ(part->Id, partId);
    }

    /// @brief Verifies that Get() returns nullptr for a non-existent entity.
    TEST(AssemblyCollection, Get_NonExistentEntity_NullptrReturned)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId nonExistentId = idGen.NextId();

        Part* part = collection.Get(nonExistentId);
        EXPECT_EQ(part, nullptr);
    }

    /// @brief Verifies that Has() correctly identifies existing and non-existing entities.
    TEST(AssemblyCollection, Has_ExistingAndNonExistentEntities_CorrectResultsReturned)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId existingId = idGen.NextId();
        const EntityId nonExistentId = idGen.NextId();

        collection.Add(existingId);

        EXPECT_TRUE(collection.Has(existingId));
        EXPECT_FALSE(collection.Has(nonExistentId));
    }

    /// @brief Verifies that the const version of Get() works correctly.
    TEST(AssemblyCollection, Get_ConstVersion_ValidPointerReturned)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId partId = idGen.NextId();
        collection.Add(partId);

        const AssemblyCollection& constCollection = collection;
        const Part* part = constCollection.Get(partId);

        ASSERT_NE(part, nullptr);
        EXPECT_EQ(part->Id, partId);
    }

    // ========== Iterator Tests ==========

    /// @brief Verifies that iterating over an empty collection works correctly (begin equals end).
    TEST(AssemblyCollection, Iterator_EmptyCollection_BeginEqualsEnd)
    {
        AssemblyCollection collection;

        EXPECT_EQ(collection.begin(), collection.end());
    }

    /// @brief Verifies that iteration traverses parts in depth-first order with parents before children.
    TEST(AssemblyCollection, Iterator_HierarchicalCollection_DepthFirstOrderMaintained)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId grandchild = idGen.NextId();

        collection.Add(root);
        collection.Add(child1, Part{ .Id = child1, .ParentId = root });
        collection.Add(child2, Part{ .Id = child2, .ParentId = root });
        collection.Add(grandchild, Part{ .Id = grandchild, .ParentId = child2 });

        // Verify depth-first ordering
        std::vector<EntityId> traversalOrder;
        for (const auto& part : collection)
        {
            traversalOrder.push_back(part.Id);
        }

        ASSERT_EQ(traversalOrder.size(), 4);
        EXPECT_EQ(traversalOrder[0], root);
        EXPECT_EQ(traversalOrder[1], child2);
        EXPECT_EQ(traversalOrder[2], grandchild);
        EXPECT_EQ(traversalOrder[3], child1);
    }

    /// @brief Verifies that all parts are stored contiguously in memory.
    TEST(AssemblyCollection, Iterator_MultipleParts_ContiguousMemoryVerified)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId id1 = idGen.NextId();
        const EntityId id2 = idGen.NextId();
        const EntityId id3 = idGen.NextId();

        collection.Add(id1);
        collection.Add(id2, Part{ .Id = id2, .ParentId = id1 });
        collection.Add(id3, Part{ .Id = id3, .ParentId = id1 });

        const Part* prevPart = nullptr;
        for (const auto& part : collection)
        {
            if (prevPart)
            {
                EXPECT_EQ(&part, prevPart + 1) << "Parts are not contiguous in memory";
            }
            prevPart = &part;
        }
    }

    // ========== Edge Cases & Stress Tests ==========

    /// @brief Verifies that the collection handles entities with large ID values correctly.
    TEST(AssemblyCollection, Add_EntityWithLargeIdValue_HandledCorrectly)
    {
        AssemblyCollection collection;
        EcsRegistry registry;

        // Create entities with large ID values by creating and destroying many
        for (int i = 0; i < 1000; ++i)
        {
            // Satisfy [[nodiscard]] warning
            auto tempId = registry.Create();
        }

        // Now create entity with large ID value
        const EntityId largeId = registry.Create();
        EXPECT_GT(largeId.Value(), 999);

        collection.Add(largeId);

        EXPECT_TRUE(collection.Has(largeId));
        EXPECT_EQ(collection.size(), 1);
        EXPECT_NE(collection.Get(largeId), nullptr);
    }

    /// @brief Verifies that a sequence of add and remove operations maintains collection integrity.
    TEST(AssemblyCollection, AddRemove_SequenceOfOperations_IntegrityMaintained)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        std::vector<EntityId> ids;
        for (int i = 0; i < 10; ++i)
        {
            ids.push_back(idGen.NextId());
        }

        // Add all as top-level
        for (const auto& id : ids)
        {
            collection.Add(id);
        }
        EXPECT_EQ(collection.size(), 10);

        // Remove every other one
        for (size_t i = 0; i < ids.size(); i += 2)
        {
            collection.Remove(ids[i]);
        }
        EXPECT_EQ(collection.size(), 5);

        // Verify remaining ones are still accessible
        for (size_t i = 1; i < ids.size(); i += 2)
        {
            EXPECT_TRUE(collection.Has(ids[i]));
        }

        // Add them back
        for (size_t i = 0; i < ids.size(); i += 2)
        {
            collection.Add(ids[i]);
        }
        EXPECT_EQ(collection.size(), 10);
    }

    /// @brief Verifies that removing an intermediate generation in a multi-level hierarchy removes all descendants.
    TEST(AssemblyCollection, Remove_IntermediateGeneration_AllDescendantsRemoved)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // 4 generation hierarchy
        const EntityId gen1 = idGen.NextId();
        const EntityId gen2 = idGen.NextId();
        const EntityId gen3 = idGen.NextId();
        const EntityId gen4 = idGen.NextId();

        collection.Add(gen1);
        collection.Add(gen2, Part{ .Id = gen2, .ParentId = gen1 });
        collection.Add(gen3, Part{ .Id = gen3, .ParentId = gen2 });
        collection.Add(gen4, Part{ .Id = gen4, .ParentId = gen3 });

        EXPECT_EQ(collection.size(), 4);

        // Remove generation 2 (should remove 2, 3, and 4)
        collection.Remove(gen2);

        EXPECT_EQ(collection.size(), 1);
        EXPECT_TRUE(collection.Has(gen1));
        EXPECT_FALSE(collection.Has(gen2));
        EXPECT_FALSE(collection.Has(gen3));
        EXPECT_FALSE(collection.Has(gen4));
    }

    /// @brief Verifies that the collection handles sparse entity IDs efficiently with appropriate index growth.
    TEST(AssemblyCollection, Add_SparseEntityIds_IndexGrowsAppropriately)
    {
        AssemblyCollection collection;
        EcsRegistry registry;

        // Create sparse IDs by creating and destroying intermediate ones
        const EntityId id1 = registry.Create();
        
        for (int i = 0; i < 50; ++i)
        {
            // Satisfy [[nodiscard]] warning
            auto tempId = registry.Create();
        }
        
        const EntityId id2 = registry.Create();

        collection.Add(id1);
        collection.Add(id2);

        EXPECT_EQ(collection.size(), 2);
        EXPECT_TRUE(collection.Has(id1));
        EXPECT_TRUE(collection.Has(id2));
    }

    // ========== Complex Scenario Tests ==========

    /// @brief Verifies that multiple independent hierarchies can coexist and be removed independently.
    TEST(AssemblyCollection, AddRemove_MultipleIndependentHierarchies_IndependentManagement)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // Create two separate hierarchies
        const EntityId root1 = idGen.NextId();
        const EntityId root1_child1 = idGen.NextId();
        const EntityId root1_child2 = idGen.NextId();

        const EntityId root2 = idGen.NextId();
        const EntityId root2_child1 = idGen.NextId();
        const EntityId root2_child2 = idGen.NextId();

        collection.Add(root1);
        collection.Add(root1_child1, Part{ .Id = root1_child1, .ParentId = root1 });
        collection.Add(root1_child2, Part{ .Id = root1_child2, .ParentId = root1 });

        collection.Add(root2);
        collection.Add(root2_child1, Part{ .Id = root2_child1, .ParentId = root2 });
        collection.Add(root2_child2, Part{ .Id = root2_child2, .ParentId = root2 });
        EXPECT_EQ(collection.size(), 6);

        // Remove first hierarchy
        collection.Remove(root1);

        EXPECT_EQ(collection.size(), 3);
        EXPECT_FALSE(collection.Has(root1));
        EXPECT_FALSE(collection.Has(root1_child1));
        EXPECT_FALSE(collection.Has(root1_child2));
        EXPECT_TRUE(collection.Has(root2));
        EXPECT_TRUE(collection.Has(root2_child1));
        EXPECT_TRUE(collection.Has(root2_child2));
    }

    /// @brief Verifies that adding grandchildren after siblings maintains correct hierarchical ordering.
    TEST(AssemblyCollection, Add_GrandchildrenAfterSiblings_CorrectOrderingMaintained)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId child3 = idGen.NextId();

        collection.Add(root);
        
        // Add children, but later add grandchildren to first child
        // Note the order in the collection is the reverse of addition order
        collection.Add(child1, Part{ .Id = child1, .ParentId = root });
        collection.Add(child2, Part{ .Id = child2, .ParentId = root });
        collection.Add(child3, Part{ .Id = child3, .ParentId = root });

        // Now add grandchild to child2 (should insert after child2)
        const EntityId grandchild1 = idGen.NextId();
        collection.Add(grandchild1, Part{ .Id = grandchild1, .ParentId = child2 });
        // Expected order: root, child3, child2, grandchild1, child1
        auto it = collection.begin();
        EXPECT_EQ(it->Id, root); ++it;
        EXPECT_EQ(it->Id, child3); ++it;
        EXPECT_EQ(it->Id, child2); ++it;
        EXPECT_EQ(it->Id, grandchild1); ++it;
        EXPECT_EQ(it->Id, child1);
    }

    /// @brief Verifies that removing a single leaf from a complex hierarchy preserves the remaining structure.
    TEST(AssemblyCollection, Remove_SingleLeafFromComplexHierarchy_RemainingStructurePreserved)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // Build complex tree
        const EntityId root = idGen.NextId();
        const EntityId branch1 = idGen.NextId();
        const EntityId branch2 = idGen.NextId();
        const EntityId leaf1_1 = idGen.NextId();
        const EntityId leaf1_2 = idGen.NextId();
        const EntityId leaf2_1 = idGen.NextId();

        collection.Add(root);
        collection.Add(branch1, Part{ .Id = branch1, .ParentId = root });
        collection.Add(leaf1_1, Part{ .Id = leaf1_1, .ParentId = branch1 });
        collection.Add(leaf1_2, Part{ .Id = leaf1_2, .ParentId = branch1 });
        collection.Add(branch2, Part{ .Id = branch2, .ParentId = root });
        collection.Add(leaf2_1, Part{ .Id = leaf2_1, .ParentId = branch2 });
        // Remove one leaf
        collection.Remove(leaf1_1);

        EXPECT_EQ(collection.size(), 5);
        EXPECT_FALSE(collection.Has(leaf1_1));
        
        // Verify structure is preserved
        EXPECT_TRUE(collection.Has(root));
        EXPECT_TRUE(collection.Has(branch1));
        EXPECT_TRUE(collection.Has(leaf1_2));
        EXPECT_TRUE(collection.Has(branch2));
        EXPECT_TRUE(collection.Has(leaf2_1));

        // Verify relationships
        EXPECT_EQ(collection.Get(leaf1_2)->ParentId, branch1);
        EXPECT_EQ(collection.Get(leaf2_1)->ParentId, branch2);
    }

    // ========== Memory Layout Tests ==========

    /// @brief Verifies that all items in the collection are stored contiguously in physical memory.
    TEST(AssemblyCollection, MemoryLayout_AllItems_StoredContiguouslyInPhysicalMemory)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // Build a complex hierarchy with multiple levels and branches
        const EntityId root1 = idGen.NextId();
        const EntityId child1_1 = idGen.NextId();
        const EntityId child1_2 = idGen.NextId();
        const EntityId grandchild1_1_1 = idGen.NextId();
        
        const EntityId root2 = idGen.NextId();
        const EntityId child2_1 = idGen.NextId();

        collection.Add(root1);
        collection.Add(child1_1, Part{ .Id = child1_1, .ParentId = root1 });
        collection.Add(grandchild1_1_1, Part{ .Id = grandchild1_1_1, .ParentId = child1_1 });
        collection.Add(child1_2, Part{ .Id = child1_2, .ParentId = root1 });
        collection.Add(root2);
        collection.Add(child2_1, Part{ .Id = child2_1, .ParentId = root2 });

        ASSERT_GT(collection.size(), 1) << "Need at least 2 parts for contiguity test";

        // Get pointer to first element
        auto it = collection.begin();
        const Part* firstPart = &(*it);
        
        // Verify all parts are in a contiguous block of memory
        size_t index = 0;
        for (const auto& part : collection)
        {
            const Part* expectedAddress = firstPart + index;
            const Part* actualAddress = &part;
            
            EXPECT_EQ(actualAddress, expectedAddress) 
                << "Part at index " << index << " is not at expected memory address. "
                << "Expected: " << expectedAddress << ", Actual: " << actualAddress;
            
            ++index;
        }

        EXPECT_EQ(index, collection.size()) << "Iterator count should match collection size";
    }

    /// @brief Verifies that each hierarchy (parts sharing a common ancestor) is stored contiguously in physical memory.
    TEST(AssemblyCollection, MemoryLayout_EachHierarchy_StoredContiguouslyInPhysicalMemory)
    {
        AssemblyCollection collection;
        TestIdGenerator idGen;

        // Create first hierarchy (3 levels deep)
        const EntityId root1 = idGen.NextId();
        const EntityId root1_child1 = idGen.NextId();
        const EntityId root1_child2 = idGen.NextId();
        const EntityId root1_grandchild1 = idGen.NextId();
        const EntityId root1_grandchild2 = idGen.NextId();

        collection.Add(root1);
        collection.Add(root1_child1, Part{ .Id = root1_child1, .ParentId = root1 });
        collection.Add(root1_grandchild1, Part{ .Id = root1_grandchild1, .ParentId = root1_child1 });
        collection.Add(root1_grandchild2, Part{ .Id = root1_grandchild2, .ParentId = root1_child1 });
        collection.Add(root1_child2, Part{ .Id = root1_child2, .ParentId = root1 });

        // Create second hierarchy (2 levels deep)
        const EntityId root2 = idGen.NextId();
        const EntityId root2_child1 = idGen.NextId();
        const EntityId root2_child2 = idGen.NextId();

        collection.Add(root2);
        collection.Add(root2_child1, Part{ .Id = root2_child1, .ParentId = root2 });
        collection.Add(root2_child2, Part{ .Id = root2_child2, .ParentId = root2 });

        // Create third hierarchy (single level)
        const EntityId root3 = idGen.NextId();
        collection.Add(root3);

        EXPECT_EQ(collection.size(), 9);

        // Define hierarchies - all parts that share a common ancestor
        std::vector<EntityId> hierarchy1_ids = { root1, root1_child1, root1_grandchild1, root1_grandchild2, root1_child2 };
        std::vector<EntityId> hierarchy2_ids = { root2, root2_child1, root2_child2 };
        std::vector<EntityId> hierarchy3_ids = { root3 };

        // Helper to verify a hierarchy is stored contiguously in physical memory
        auto verifyHierarchyContiguity = [&](const std::vector<EntityId>& hierarchyIds, const std::string& hierarchyName) {
            // Get pointers to all parts in this hierarchy
            std::vector<const Part*> hierarchyParts;
            for (const auto& id : hierarchyIds)
            {
                const Part* part = collection.Get(id);
                ASSERT_NE(part, nullptr) << hierarchyName << " part not found";
                hierarchyParts.push_back(part);
            }

            // Sort parts by memory address for efficient contiguity checking
            std::sort(hierarchyParts.begin(), hierarchyParts.end());
            
            // The first part in memory order
            const Part* firstPart = hierarchyParts[0];
            
            // Verify all parts in the hierarchy are contiguous starting from firstPart
            for (size_t i = 0; i < hierarchyParts.size(); ++i)
            {
                const Part* expectedAddress = firstPart + i;
                const Part* actualAddress = hierarchyParts[i];
                
                EXPECT_EQ(actualAddress, expectedAddress)
                    << hierarchyName << ": Part at position " << i << " is not contiguous. "
                    << "Expected address: " << expectedAddress << ", Actual: " << actualAddress;
                
                // Verify physical adjacency using byte distance
                if (i > 0)
                {
                    const ptrdiff_t byteDistance = reinterpret_cast<const char*>(actualAddress) - 
                                                   reinterpret_cast<const char*>(hierarchyParts[i - 1]);
                    EXPECT_EQ(byteDistance, static_cast<ptrdiff_t>(sizeof(Part)))
                        << hierarchyName << ": Gap detected between parts at position " << (i - 1) 
                        << " and " << i << ". Distance in bytes: " << byteDistance;
                }
            }
        };

        // Verify each hierarchy is stored contiguously
        verifyHierarchyContiguity(hierarchy1_ids, "Hierarchy 1");
        verifyHierarchyContiguity(hierarchy2_ids, "Hierarchy 2");
        verifyHierarchyContiguity(hierarchy3_ids, "Hierarchy 3");

        // Also verify the entire collection is contiguous (all hierarchies together)
        auto it = collection.begin();
        const Part* baseAddress = &(*it);
        
        size_t index = 0;
        for (const auto& part : collection)
        {
            const Part* expectedAddress = baseAddress + index;
            const Part* actualAddress = &part;
            
            EXPECT_EQ(actualAddress, expectedAddress)
                << "Collection part at index " << index << " is not contiguous with the rest";
            
            ++index;
        }
    }

    // ========== Stress Tests ==========

    /// @brief Helper structure to track hierarchy information during stress testing.
    struct HierarchyInfo
    {
        EntityId root;
        std::vector<EntityId> children;
        std::vector<EntityId> grandchildren;
    };

    /// @brief Adds a new multi-level hierarchy with random children and grandchildren.
    static void AddNewHierarchy(AssemblyCollection& collection, TestIdGenerator& idGen, std::vector<HierarchyInfo>& hierarchies)
    {
        HierarchyInfo hierarchy;
        hierarchy.root = idGen.NextId();
        collection.Add(hierarchy.root);

        // Add 1-5 children
        const int numChildren = (rand() % 5) + 1;
        for (int i = 0; i < numChildren; ++i)
        {
            EntityId child = idGen.NextId();
            collection.Add(child, Part{ .Id = child, .ParentId = hierarchy.root });
            hierarchy.children.push_back(child);

            // 50% chance to add grandchildren to this child
            if (rand() % 2 == 0)
            {
                const int numGrandchildren = (rand() % 3) + 1;
                for (int j = 0; j < numGrandchildren; ++j)
                {
                    EntityId grandchild = idGen.NextId();
                    collection.Add(grandchild, Part{ .Id = grandchild, .ParentId = child });
                    hierarchy.grandchildren.push_back(grandchild);
                }
            }
        }

        hierarchies.push_back(hierarchy);
    }

    /// @brief Adds parts to an existing hierarchy (either children or grandchildren).
    static void AddToExistingHierarchy(AssemblyCollection& collection, TestIdGenerator& idGen, std::vector<HierarchyInfo>& hierarchies)
    {
        if (hierarchies.empty())
        {
            return;
        }

        const size_t hierarchyIdx = rand() % hierarchies.size();
        auto& hierarchy = hierarchies[hierarchyIdx];

        if (rand() % 2 == 0 && !hierarchy.children.empty())
        {
            // Add a grandchild to a random child
            const size_t childIdx = rand() % hierarchy.children.size();
            EntityId grandchild = idGen.NextId();
            collection.Add(grandchild, Part{ .Id = grandchild, .ParentId = hierarchy.children[childIdx] });
            hierarchy.grandchildren.push_back(grandchild);
        }
        else
        {
            // Add a new child to the root
            EntityId child = idGen.NextId();
            collection.Add(child, Part{ .Id = child, .ParentId = hierarchy.root });
            hierarchy.children.push_back(child);
        }
    }

    /// @brief Removes an entire hierarchy and verifies all parts are removed.
    static void RemoveEntireHierarchy(AssemblyCollection& collection, std::vector<HierarchyInfo>& hierarchies)
    {
        if (hierarchies.empty())
        {
            return;
        }

        const size_t hierarchyIdx = rand() % hierarchies.size();
        const auto& hierarchy = hierarchies[hierarchyIdx];

        // Verify hierarchy exists before removal
        EXPECT_TRUE(collection.Has(hierarchy.root));

        collection.Remove(hierarchy.root);

        // Verify entire hierarchy is removed
        EXPECT_FALSE(collection.Has(hierarchy.root));
        for (const auto& child : hierarchy.children)
        {
            EXPECT_FALSE(collection.Has(child));
        }
        for (const auto& grandchild : hierarchy.grandchildren)
        {
            EXPECT_FALSE(collection.Has(grandchild));
        }

        // Remove from tracking
        hierarchies.erase(hierarchies.begin() + hierarchyIdx);
    }

    /// @brief Removes a partial hierarchy (middle node with its descendants).
    static void RemovePartialHierarchy(AssemblyCollection& collection, std::vector<HierarchyInfo>& hierarchies)
    {
        if (hierarchies.empty())
        {
            return;
        }

        const size_t hierarchyIdx = rand() % hierarchies.size();
        auto& hierarchy = hierarchies[hierarchyIdx];

        if (!hierarchy.children.empty())
        {
            // Remove a child (and its descendants)
            const size_t childIdx = rand() % hierarchy.children.size();
            EntityId childToRemove = hierarchy.children[childIdx];

            collection.Remove(childToRemove);

            // Verify child is removed
            EXPECT_FALSE(collection.Has(childToRemove));

            // Remove from tracking
            hierarchy.children.erase(hierarchy.children.begin() + childIdx);

            // Remove associated grandchildren from tracking
            hierarchy.grandchildren.erase(
                std::remove_if(hierarchy.grandchildren.begin(), hierarchy.grandchildren.end(),
                    [&](EntityId id) { return !collection.Has(id); }),
                hierarchy.grandchildren.end());

            // If hierarchy has no children left, remove it from tracking
            if (hierarchy.children.empty())
            {
                collection.Remove(hierarchy.root);
                hierarchies.erase(hierarchies.begin() + hierarchyIdx);
            }
        }
    }

    /// @brief Adds standalone top-level parts and randomly removes half of them.
    static void AddAndRemoveStandaloneParts(AssemblyCollection& collection, TestIdGenerator& idGen)
    {
        const int numParts = (rand() % 5) + 1;
        for (int i = 0; i < numParts; ++i)
        {
            EntityId standalone = idGen.NextId();
            collection.Add(standalone);
            
            // Immediately remove half of them (stress test)
            if (rand() % 2 == 0)
            {
                collection.Remove(standalone);
            }
        }
    }

    /// @brief Verifies collection integrity including hierarchy relationships and memory contiguity.
    static void VerifyCollectionIntegrity(const AssemblyCollection& collection, const std::vector<HierarchyInfo>& hierarchies, int iteration)
    {
        // Verify all tracked hierarchies still exist correctly
        for (const auto& hierarchy : hierarchies)
        {
            EXPECT_TRUE(collection.Has(hierarchy.root)) 
                << "Iteration " << iteration << ": Root should exist";
            
            for (const auto& child : hierarchy.children)
            {
                EXPECT_TRUE(collection.Has(child)) 
                    << "Iteration " << iteration << ": Child should exist";
                EXPECT_EQ(collection.Get(child)->ParentId, hierarchy.root)
                    << "Iteration " << iteration << ": Child should have correct parent";
            }
        }

        // Verify memory contiguity
        if (collection.size() > 1)
        {
            auto it = collection.begin();
            const Part* prevPart = &(*it);
            ++it;
            
            for (; it != collection.end(); ++it)
            {
                const Part* currentPart = &(*it);
                EXPECT_EQ(currentPart, prevPart + 1) 
                    << "Iteration " << iteration << ": Parts not contiguous in memory";
                prevPart = currentPart;
            }
        }
    }

    /// @brief Performs final verification of all remaining hierarchies and memory contiguity.
    static void VerifyFinalState(const AssemblyCollection& collection, const std::vector<HierarchyInfo>& hierarchies)
    {
        // Verify all remaining hierarchies
        for (const auto& hierarchy : hierarchies)
        {
            EXPECT_TRUE(collection.Has(hierarchy.root)) << "Final: Root should exist";
            
            const Part* rootPart = collection.Get(hierarchy.root);
            ASSERT_NE(rootPart, nullptr);
            EXPECT_FALSE(rootPart->ParentId.IsValid()) << "Final: Root should have no parent";

            for (const auto& child : hierarchy.children)
            {
                EXPECT_TRUE(collection.Has(child)) << "Final: Child should exist";
                const Part* childPart = collection.Get(child);
                ASSERT_NE(childPart, nullptr);
                EXPECT_EQ(childPart->ParentId, hierarchy.root) << "Final: Child should have correct parent";
            }

            for (const auto& grandchild : hierarchy.grandchildren)
            {
                EXPECT_TRUE(collection.Has(grandchild)) << "Final: Grandchild should exist";
            }
        }

        // Verify final memory contiguity
        if (collection.size() > 0)
        {
            auto it = collection.begin();
            const Part* firstPart = &(*it);
            
            size_t index = 0;
            for (const auto& part : collection)
            {
                const Part* expectedAddress = firstPart + index;
                const Part* actualAddress = &part;
                
                EXPECT_EQ(actualAddress, expectedAddress) 
                    << "Final: Part at index " << index << " not contiguous";
                
                ++index;
            }
        }
    }

    /// @brief Initializes a collection with a specified number of items in random hierarchies.
    static void InitializeCollectionWithRandomHierarchies(AssemblyCollection& collection, TestIdGenerator& idGen, 
                                                          std::vector<HierarchyInfo>& activeHierarchies, size_t targetItemCount)
    {
        std::cout << "Initializing collection with " << targetItemCount << " items...\n";
        size_t totalItemsAdded = 0;
        
        while (totalItemsAdded < targetItemCount)
        {
            const int hierarchyType = rand() % 10;
            
            if (hierarchyType < 2)
            {
                // Add a standalone top-level part (20% chance)
                EntityId standalone = idGen.NextId();
                collection.Add(standalone);
                totalItemsAdded++;
            }
            else if (hierarchyType < 5)
            {
                // Add a simple 2-level hierarchy: 1 root + 1-3 children (30% chance)
                HierarchyInfo hierarchy;
                hierarchy.root = idGen.NextId();
                collection.Add(hierarchy.root);
                totalItemsAdded++;

                const int numChildren = (rand() % 3) + 1;
                for (int i = 0; i < numChildren && totalItemsAdded < targetItemCount; ++i)
                {
                    EntityId child = idGen.NextId();
                    collection.Add(child, Part{ .Id = child, .ParentId = hierarchy.root });
                    hierarchy.children.push_back(child);
                    totalItemsAdded++;
                }

                activeHierarchies.push_back(hierarchy);
            }
            else if (hierarchyType < 8)
            {
                // Add a medium 3-level hierarchy: 1 root + 2-4 children + some grandchildren (30% chance)
                HierarchyInfo hierarchy;
                hierarchy.root = idGen.NextId();
                collection.Add(hierarchy.root);
                totalItemsAdded++;

                const int numChildren = (rand() % 3) + 2;
                for (int i = 0; i < numChildren && totalItemsAdded < targetItemCount; ++i)
                {
                    EntityId child = idGen.NextId();
                    collection.Add(child, Part{ .Id = child, .ParentId = hierarchy.root });
                    hierarchy.children.push_back(child);
                    totalItemsAdded++;

                    // 60% chance to add 1-2 grandchildren
                    if (rand() % 10 < 6)
                    {
                        const int numGrandchildren = (rand() % 2) + 1;
                        for (int j = 0; j < numGrandchildren && totalItemsAdded < targetItemCount; ++j)
                        {
                            EntityId grandchild = idGen.NextId();
                            collection.Add(grandchild, Part{ .Id = grandchild, .ParentId = child });
                            hierarchy.grandchildren.push_back(grandchild);
                            totalItemsAdded++;
                        }
                    }
                }

                activeHierarchies.push_back(hierarchy);
            }
            else
            {
                // Add a deep 4+ level hierarchy with multiple branches (20% chance)
                HierarchyInfo hierarchy;
                hierarchy.root = idGen.NextId();
                collection.Add(hierarchy.root);
                totalItemsAdded++;

                const int numChildren = (rand() % 4) + 3;
                for (int i = 0; i < numChildren && totalItemsAdded < targetItemCount; ++i)
                {
                    EntityId child = idGen.NextId();
                    collection.Add(child, Part{ .Id = child, .ParentId = hierarchy.root });
                    hierarchy.children.push_back(child);
                    totalItemsAdded++;

                    // Add 2-5 grandchildren to each child
                    const int numGrandchildren = (rand() % 4) + 2;
                    for (int j = 0; j < numGrandchildren && totalItemsAdded < targetItemCount; ++j)
                    {
                        EntityId grandchild = idGen.NextId();
                        collection.Add(grandchild, Part{ .Id = grandchild, .ParentId = child });
                        hierarchy.grandchildren.push_back(grandchild);
                        totalItemsAdded++;

                        // 40% chance to add great-grandchildren (4th level)
                        if (rand() % 10 < 4)
                        {
                            const int numGreatGrandchildren = (rand() % 3) + 1;
                            for (int k = 0; k < numGreatGrandchildren && totalItemsAdded < targetItemCount; ++k)
                            {
                                EntityId greatGrandchild = idGen.NextId();
                                collection.Add(greatGrandchild, Part{ .Id = greatGrandchild, .ParentId = grandchild });
                                totalItemsAdded++;
                            }
                        }
                    }
                }

                activeHierarchies.push_back(hierarchy);
            }
        }

        std::cout << "Initialization complete. Collection size: " << collection.size() 
                  << ", Active hierarchies: " << activeHierarchies.size() << "\n";
        
        EXPECT_EQ(collection.size(), targetItemCount) 
            << "Collection should be initialized with exactly " << targetItemCount << " items";

        // Verify initial memory contiguity
        if (collection.size() > 1)
        {
            auto it = collection.begin();
            const Part* firstPart = &(*it);
            
            size_t index = 0;
            for (const auto& part : collection)
            {
                const Part* expectedAddress = firstPart + index;
                const Part* actualAddress = &part;
                
                EXPECT_EQ(actualAddress, expectedAddress) 
                    << "Initial: Part at index " << index << " not contiguous in memory";
                
                ++index;
            }
        }
    }

    /// @brief Stress test that performs many add/remove operations on a large collection with complex hierarchies.
    TEST(AssemblyCollection, StressTest_ManyOperationsOnLargeCollection_IntegrityMaintained)
    {
        // Seed RNG with current time for randomness, but log it for reproducibility
        const unsigned int seed = static_cast<unsigned int>(std::time(nullptr));
        std::srand(seed);
        std::cout << "Stress test RNG seed: " << seed << " (use this seed to reproduce the test sequence)\n";

        AssemblyCollection collection;
        TestIdGenerator idGen;
        std::vector<HierarchyInfo> activeHierarchies;

        const int NUM_ITERATIONS = 1000;
        const int MAX_HIERARCHIES = 50;

        // Initialize collection with 10,000 items in random hierarchies
        InitializeCollectionWithRandomHierarchies(collection, idGen, activeHierarchies, 10000);

        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration)
        {
            const int operation = rand() % 10; // 0-9 for different operations

            if (operation < 3 && activeHierarchies.size() < MAX_HIERARCHIES)
            {
                // Add a new multi-level hierarchy (30% chance)
                AddNewHierarchy(collection, idGen, activeHierarchies);
            }
            else if (operation < 5)
            {
                // Add parts to an existing hierarchy (20% chance)
                AddToExistingHierarchy(collection, idGen, activeHierarchies);
            }
            else if (operation < 7)
            {
                // Remove an entire hierarchy (20% chance)
                RemoveEntireHierarchy(collection, activeHierarchies);
            }
            else if (operation < 9)
            {
                // Remove a partial hierarchy (middle node) (20% chance)
                RemovePartialHierarchy(collection, activeHierarchies);
            }
            else
            {
                // Add some standalone top-level parts (10% chance)
                AddAndRemoveStandaloneParts(collection, idGen);
            }

            // Periodically verify collection integrity
            if (iteration % 10 == 0)
            {
                VerifyCollectionIntegrity(collection, activeHierarchies, iteration);
            }
        }

        // Final verification
        VerifyFinalState(collection, activeHierarchies);
    }
}
