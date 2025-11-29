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

    /// @brief Generates a random transform (translation, rotation, scale) for testing.
    /// @return A randomly generated TrsTransformf.
    static TrsTransformf RandomTrsTransform()
    {
        static std::mt19937 rng(std::random_device{}());

        std::uniform_real_distribution<float> tDist(-100.f, 100.f);
        std::uniform_real_distribution<float> scaleDist(0.1f, 5.f);
        std::uniform_real_distribution<float> axisDist(-1.f, 1.f);
        std::uniform_real_distribution<float> angleDist(0.f, 2.f * std::numbers::pi_v<float>);

        Vec3f axis{ axisDist(rng), axisDist(rng), axisDist(rng) };
        // Ensure axis is non-zero; fallback to Y axis if too small.
        if (glm::length(static_cast<glm::vec3>(axis)) < 1e-4f)
        {
            axis = Vec3f{ 0.f, 1.f, 0.f };
        }
        axis = axis.Normalize();

        Radiansf angle(angleDist(rng));
        Quatf rotation(angle, axis);

        return TrsTransformf{
            .T{ tDist(rng), tDist(rng), tDist(rng) },
            .R{ rotation },
            .S{ scaleDist(rng), scaleDist(rng), scaleDist(rng) }
        };
    }

    // ========== Query/Access Tests ==========

    /// @brief Verifies that indexer returns a valid component for an existing entity.
    TEST(EcsTransformNodePool, Get_ExistingEntity_ValidPointerReturned)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId nodeId = idGen.NextId();
        auto xform = RandomTrsTransform();
        pool.Add(nodeId, TransformNode2{ .LocalTransform = xform });

        auto node = pool[nodeId];
        // Validate LocalTransform default scale instead of removed Id member
        EXPECT_EQ(node.LocalTransform, xform);
    }

    /// @brief Verifies that Has() correctly identifies existing and non-existing entities.
    TEST(EcsTransformNodePool, Has_ExistingAndNonExistentEntities_CorrectResultsReturned)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId existingId = idGen.NextId();
        const EntityId nonExistentId = idGen.NextId();

        pool.Add(existingId, {});

        EXPECT_TRUE(pool.Has(existingId));
        EXPECT_FALSE(pool.Has(nonExistentId));
    }

    /// @brief Verifies that the const version of operator[] works correctly.
    TEST(EcsTransformNodePool, Get_ConstVersion_ValidPointerReturned)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId nodeId = idGen.NextId();
        auto xform = RandomTrsTransform();
        pool.Add(nodeId, TransformNode2{ .LocalTransform = xform });

        const EcsComponentPool<TransformNode2>& constpool = pool;
        const TransformNode2& node = constpool[nodeId];

        EXPECT_EQ(node.LocalTransform, xform);
    }

    // ========== Basic Operations Tests ==========

    /// @brief Verifies that a single top-level node can be added and retrieved correctly.
    TEST(EcsTransformNodePool, Add_SingleTopLevelNode_NodeAddedSuccessfully)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId eid = idGen.NextId();
        auto localTransform = RandomTrsTransform();
        pool.Add(eid, TransformNode2{ EntityId{}, localTransform });

        EXPECT_EQ(pool.size(), 1);
        EXPECT_TRUE(pool.Has(eid));
        
        const TransformNode2& node = pool[eid];
        EXPECT_EQ(node.LocalTransform, localTransform);
        EXPECT_FALSE(node.ParentId.IsValid());
    }

    /// @brief Verifies that multiple top-level nodes can be added independently.
    TEST(EcsTransformNodePool, Add_MultipleTopLevelNodes_AllNodesAddedSuccessfully)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId id1 = idGen.NextId();
        const EntityId id2 = idGen.NextId();
        const EntityId id3 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(id1, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(id2, TransformNode2{ .LocalTransform = localTransforms[1] });
        pool.Add(id3, TransformNode2{ .LocalTransform = localTransforms[2] });
        EXPECT_EQ(pool.size(), 3);
        EXPECT_TRUE(pool.Has(id1));
        EXPECT_TRUE(pool.Has(id2));
        EXPECT_TRUE(pool.Has(id3));

        const TransformNode2& node1 = pool[id1];
        const TransformNode2& node2 = pool[id2];
        const TransformNode2& node3 = pool[id3];
        
        EXPECT_EQ(node1.LocalTransform, localTransforms[0]);
        EXPECT_EQ(node2.LocalTransform, localTransforms[1]);
        EXPECT_EQ(node3.LocalTransform, localTransforms[2]);

        EXPECT_FALSE(node1.ParentId.IsValid());
        EXPECT_FALSE(node2.ParentId.IsValid());
        EXPECT_FALSE(node3.ParentId.IsValid());
    }

    /// @brief Verifies that a child node is added after its parent and maintains correct parent-child relationship.
    TEST(EcsTransformNodePool, Add_SingleChildToParent_ChildAddedAfterParent)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId parentId = idGen.NextId();
        const EntityId childId = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(parentId, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(childId, TransformNode2{ parentId, localTransforms[1] });

        EXPECT_EQ(pool.size(), 2);
        EXPECT_TRUE(pool.Has(parentId));
        EXPECT_TRUE(pool.Has(childId));

        const TransformNode2& child = pool[childId];
        EXPECT_EQ(child.LocalTransform, localTransforms[1]);
        EXPECT_EQ(child.ParentId, parentId);

        // Verify child appears after parent in iteration order
        auto it = pool.begin();
        EXPECT_EQ(*it, parentId);
        const TransformNode2& parentNode = pool[*it];
        ++it;
        EXPECT_EQ(*it, childId);
        const TransformNode2& childNode = pool[*it];

        //Verify contiguity
        EXPECT_EQ(&parentNode + 1, &childNode);
    }

    /// @brief Verifies that multiple children are added consecutively after their parent with correct relationships.
    TEST(EcsTransformNodePool, Add_MultipleChildrenToParent_AllChildrenAddedConsecutively)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId parentId = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId child3 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(parentId, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(child1, TransformNode2{ parentId, localTransforms[1] });
        pool.Add(child2, TransformNode2{ parentId, localTransforms[2] });
        pool.Add(child3, TransformNode2{ parentId, localTransforms[3] });

        EXPECT_EQ(pool.size(), 4);

        // Verify all children have correct parent
        EXPECT_EQ(pool[child1].ParentId, parentId);
        EXPECT_EQ(pool[child2].ParentId, parentId);
        EXPECT_EQ(pool[child3].ParentId, parentId);

        // Verify all entities have correct data
        EXPECT_EQ(pool[parentId].LocalTransform, localTransforms[0]);
        EXPECT_EQ(pool[child1].LocalTransform, localTransforms[1]);
        EXPECT_EQ(pool[child2].LocalTransform, localTransforms[2]);
        EXPECT_EQ(pool[child3].LocalTransform, localTransforms[3]);

        // Verify ordering: parent followed by all children, children in reverse order of addition
        TransformNode2* ptrs[std::size(localTransforms)];

        auto it = pool.begin();
        EXPECT_EQ(*it, parentId);
        ptrs[0] = &pool[*it];
        ++it;
        EXPECT_EQ(pool[*it].ParentId, parentId); // child3
        EXPECT_EQ(*it, child3);
        ptrs[1] = &pool[*it];
        ++it;
        EXPECT_EQ(pool[*it].ParentId, parentId); // child2
        EXPECT_EQ(*it, child2);
        ptrs[2] = &pool[*it];
        ++it;
        EXPECT_EQ(pool[*it].ParentId, parentId); // child1
        EXPECT_EQ(*it, child1);
        ptrs[3] = &pool[*it];

        // Verify contiguity
        for (size_t i = 0; i < std::size(ptrs) - 1; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
        }
    }

    /// @brief Verifies that attempting to add an invalid EntityId is rejected and pool remains unchanged.
    TEST(EcsTransformNodePool, Add_InvalidEntityId_AddRejected)
    {
        EcsComponentPool<TransformNode2> pool;
        
        EntityId invalidId; // Default constructor creates invalid ID
        EXPECT_FALSE(invalidId.IsValid());

        assert_capture(capture)
        {
            const bool added = pool.Add(invalidId, {});
            EXPECT_FALSE(added);
            EXPECT_TRUE(capture.Message().contains("EntityId must be valid"));
        }

        EXPECT_EQ(pool.size(), 0);
        EXPECT_FALSE(pool.Has(invalidId));
    }

    /// @brief Verifies that attempting to add a duplicate EntityId is rejected.
    TEST(EcsTransformNodePool, Add_DuplicateEntityId_AddRejected)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId nodeId = idGen.NextId();
        
        pool.Add(nodeId, {});
        EXPECT_EQ(pool.size(), 1);

        // Attempt to add same ID again
        assert_capture(capture)
        {
            const bool added = pool.Add(nodeId, {});
            EXPECT_FALSE(added);
            EXPECT_TRUE(capture.Message().contains("Entity ID already in collection"));
        }

        EXPECT_EQ(pool.size(), 1); // Size should not change
    }

    /// @brief Verifies that attempting to add an entity with itself as parent is rejected.
    TEST(EcsTransformNodePool, Add_EntityWithSelfAsParent_AddRejected)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId nodeId = idGen.NextId();
        
        // Add as top-level first
        pool.Add(nodeId, {});
        const size_t initialSize = pool.size();

        // Attempt to add same ID with itself as parent
        const EntityId sameId = idGen.NextId();
        assert_capture(capture)
        {
            const bool added = pool.Add(sameId, TransformNode2{ sameId, RandomTrsTransform() });
            EXPECT_FALSE(added);
            EXPECT_TRUE(capture.Message().contains("Entity cannot be its own parent"));
        }

        EXPECT_EQ(pool.size(), initialSize); // Size should not change
        EXPECT_FALSE(pool.Has(sameId));
    }

    /// @brief Verifies that attempting to add a child with a non-existent parent is rejected.
    TEST(EcsTransformNodePool, Add_ChildWithNonExistentParent_AddRejected)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId childId = idGen.NextId();
        const EntityId nonExistentParent = idGen.NextId();

        // Attempt to add child with parent that doesn't exist
        assert_capture(capture)
        {
            const bool added = pool.Add(childId, TransformNode2{ nonExistentParent, RandomTrsTransform() });
            EXPECT_FALSE(added);
            EXPECT_TRUE(capture.Message().contains("Parent ID not found in collection"));
        }

        EXPECT_EQ(pool.size(), 0);
        EXPECT_FALSE(pool.Has(childId));
    }

    // ========== Hierarchical Structure Tests ==========

    /// @brief Verifies that a three-level nested hierarchy maintains correct ordering and relationships.
    TEST(EcsTransformNodePool, Add_ThreeLevelNestedHierarchy_CorrectOrderingMaintained)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId grandparent = idGen.NextId();
        const EntityId parent = idGen.NextId();
        const EntityId child = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(grandparent, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(parent, TransformNode2{ grandparent, localTransforms[1] });
        pool.Add(child, TransformNode2{ parent, localTransforms[2] });
        EXPECT_EQ(pool.size(), 3);

        // Verify relationships
        EXPECT_FALSE(pool[grandparent].ParentId.IsValid());
        EXPECT_EQ(pool[parent].ParentId, grandparent);
        EXPECT_EQ(pool[child].ParentId, parent);

        // Verify ordering
        TransformNode2* ptrs[std::size(localTransforms)];
        
        auto it = pool.begin();
        EXPECT_EQ(*it, grandparent);
        ptrs[0] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, parent);
        ptrs[1] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child);
        ptrs[2] = &pool[*it];

        // Verify contiguity
        for (size_t i = 0; i < std::size(ptrs) - 1; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
        }
    }

    /// @brief Verifies that a hierarchy with multiple branches and grandchildren maintains proper structure.
    TEST(EcsTransformNodePool, Add_MultipleBranchesWithGrandchildren_ProperStructureMaintained)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId grandchild1_1 = idGen.NextId();
        const EntityId grandchild1_2 = idGen.NextId();
        const EntityId grandchild2_1 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(root, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(child1, TransformNode2{ root, localTransforms[1] });
        pool.Add(grandchild1_1, TransformNode2{ child1, localTransforms[2] });
        pool.Add(grandchild1_2, TransformNode2{ child1, localTransforms[3] });
        pool.Add(child2, TransformNode2{ root, localTransforms[4] });
        pool.Add(grandchild2_1, TransformNode2{ child2, localTransforms[5] });
        EXPECT_EQ(pool.size(), 6);

        // Verify all nodes exist
        EXPECT_TRUE(pool.Has(root));
        EXPECT_TRUE(pool.Has(child1));
        EXPECT_TRUE(pool.Has(child2));
        EXPECT_TRUE(pool.Has(grandchild1_1));
        EXPECT_TRUE(pool.Has(grandchild1_2));
        EXPECT_TRUE(pool.Has(grandchild2_1));

        // Verify relationships
        EXPECT_EQ(pool[child1].ParentId, root);
        EXPECT_EQ(pool[child2].ParentId, root);
        EXPECT_EQ(pool[grandchild1_1].ParentId, child1);
        EXPECT_EQ(pool[grandchild1_2].ParentId, child1);
        EXPECT_EQ(pool[grandchild2_1].ParentId, child2);

        // Verify depth first ordering
        TransformNode2* ptrs[std::size(localTransforms)];

        auto it = pool.begin();
        EXPECT_EQ(*it, root);
        ptrs[0] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child2);
        ptrs[1] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, grandchild2_1);
        ptrs[2] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child1);
        ptrs[3] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, grandchild1_2);
        ptrs[4] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, grandchild1_1);
        ptrs[5] = &pool[*it];

        // Verify contiguity
        for (size_t i = 0; i < std::size(ptrs) - 1; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
        }
    }

    /// @brief Verifies that adding a child to a middle node correctly inserts it and updates indices.
    TEST(EcsTransformNodePool, Add_ChildToMiddleNode_InsertedCorrectlyWithUpdatedIndices)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        // Build initial hierarchy
        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(root, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(child1, TransformNode2{ root, localTransforms[1] });
        pool.Add(child2, TransformNode2{ root, localTransforms[2] });

        EXPECT_EQ(pool.size(), 3);

        // Add a new child to the second child (middle of hierarchy)
        const EntityId grandchild = idGen.NextId();
        pool.Add(grandchild, TransformNode2{ child2, localTransforms[3] });

        EXPECT_EQ(pool.size(), 4);
        EXPECT_TRUE(pool.Has(grandchild));
        EXPECT_EQ(pool[grandchild].ParentId, child2);

        // Verify ordering: root, child2, grandchild, child1
        TransformNode2* ptrs[std::size(localTransforms)];

        auto it = pool.begin();
        EXPECT_EQ(*it, root);
        ptrs[0] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child2);
        ptrs[1] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, grandchild);
        ptrs[2] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child1);
        ptrs[3] = &pool[*it];

        // Verify contiguity
        for (size_t i = 0; i < std::size(ptrs) - 1; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
        }
    }

    // ========== Removal Tests ==========

    /// @brief Verifies that removing a top-level node without children completely removes it from the pool.
    TEST(EcsTransformNodePool, Remove_TopLevelNodeWithoutChildren_NodeRemovedCompletely)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId nodeId = idGen.NextId();
        pool.Add(nodeId, {});

        EXPECT_EQ(pool.size(), 1);
        EXPECT_TRUE(pool.Has(nodeId));
        pool.Remove(nodeId);

        EXPECT_EQ(pool.size(), 0);
        EXPECT_FALSE(pool.Has(nodeId));
    }

    /// @brief Verifies that removing a parent node also removes all its children (entire subtree).
    TEST(EcsTransformNodePool, Remove_ParentWithChildren_EntireSubtreeRemoved)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId parent = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();

        pool.Add(parent, {});
        pool.Add(child1, TransformNode2{ parent, RandomTrsTransform() });
        pool.Add(child2, TransformNode2{ parent, RandomTrsTransform() });

        EXPECT_EQ(pool.size(), 3);

        pool.Remove(parent);

        EXPECT_EQ(pool.size(), 0);
        EXPECT_FALSE(pool.Has(parent));
        EXPECT_FALSE(pool.Has(child1));
        EXPECT_FALSE(pool.Has(child2));
    }

    /// @brief Verifies that removing a middle child leaves its siblings intact.
    TEST(EcsTransformNodePool, Remove_MiddleChild_SiblingsRemainIntact)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId parent = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId child3 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(parent, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(child1, TransformNode2{ parent, localTransforms[1] });
        pool.Add(child2, TransformNode2{ parent, localTransforms[2] });
        pool.Add(child3, TransformNode2{ parent, localTransforms[3] });

        EXPECT_EQ(pool.size(), 4);

        pool.Remove(child2);

        EXPECT_EQ(pool.size(), 3);
        EXPECT_TRUE(pool.Has(parent));
        EXPECT_TRUE(pool.Has(child1));
        EXPECT_FALSE(pool.Has(child2));
        EXPECT_TRUE(pool.Has(child3));

        //Verify ordering: parent, child3, child1
        TransformNode2* ptrs[std::size(localTransforms)];

        auto it = pool.begin();
        EXPECT_EQ(*it, parent);
        ptrs[0] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child3);
        ptrs[1] = &pool[*it];
        ++it;
        EXPECT_EQ(*it, child1);
        ptrs[2] = &pool[*it];

        // Verify contiguity
        for (size_t i = 0; i < 2; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
        }
    }

    /// @brief Verifies that attempting to remove a non-existent entity has no effect on the pool.
    TEST(EcsTransformNodePool, Remove_NonExistentEntity_NoEffect)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId existingId = idGen.NextId();
        const EntityId nonExistentId = idGen.NextId();

        pool.Add(existingId, {});
        EXPECT_EQ(pool.size(), 1);

        assert_capture(capture)
        {
            pool.Remove(nonExistentId);
            EXPECT_TRUE(capture.Message().contains("Entity ID not found"));
        }

        EXPECT_EQ(pool.size(), 1);
        EXPECT_TRUE(pool.Has(existingId));
    }

    /// @brief Verifies that removing a node in a deep hierarchy removes all its descendants.
    TEST(EcsTransformNodePool, Remove_NodeInDeepHierarchy_AllDescendantsRemoved)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        // Build 4-level hierarchy
        const EntityId level1 = idGen.NextId();
        const EntityId level2 = idGen.NextId();
        const EntityId level3 = idGen.NextId();
        const EntityId level4 = idGen.NextId();
        const EntityId level3_sibling = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(level1, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(level2, TransformNode2{ level1, localTransforms[1] });
        pool.Add(level3, TransformNode2{ level2, localTransforms[2] });
        pool.Add(level4, TransformNode2{ level3, localTransforms[3] });
        pool.Add(level3_sibling, TransformNode2{ level2, localTransforms[4] });

        EXPECT_EQ(pool.size(), 5);

        // Remove middle node (level2)
        pool.Remove(level2);

        EXPECT_EQ(pool.size(), 1); // Only level1 remains
        EXPECT_TRUE(pool.Has(level1));
        EXPECT_FALSE(pool.Has(level2));
        EXPECT_FALSE(pool.Has(level3));
        EXPECT_FALSE(pool.Has(level4));
        EXPECT_FALSE(pool.Has(level3_sibling));

        EXPECT_EQ(pool[level1].LocalTransform, localTransforms[0]);
    }

    /// @brief Verifies that an entity can be removed and re-added, both as top-level and as a child.
    TEST(EcsTransformNodePool, Remove_ThenReAdd_EntityAddedSuccessfully)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId nodeId = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };
        
        // Add, remove, then add again as top-level
        pool.Add(nodeId, TransformNode2{ .LocalTransform = localTransforms[0] });
        EXPECT_TRUE(pool.Has(nodeId));
        pool.Remove(nodeId);
        EXPECT_FALSE(pool.Has(nodeId));

        pool.Add(nodeId, TransformNode2{ .LocalTransform = localTransforms[1] });
        EXPECT_TRUE(pool.Has(nodeId));
        EXPECT_EQ(pool.size(), 1);

        // Now add as child
        const EntityId parentId = idGen.NextId();
        pool.Add(parentId, TransformNode2{ .LocalTransform = localTransforms[2] });
        
        pool.Remove(nodeId);
        EXPECT_FALSE(pool.Has(nodeId));
        
        pool.Add(nodeId, TransformNode2{ parentId, localTransforms[3] });
        EXPECT_TRUE(pool.Has(nodeId));
        EXPECT_EQ(pool[nodeId].ParentId, parentId);

        auto parentPtr = &pool[parentId];
        auto childPtr = &pool[nodeId];
        EXPECT_EQ(parentPtr->LocalTransform, localTransforms[2]);
        EXPECT_EQ(childPtr->LocalTransform, localTransforms[3]);

        // Verify contiguity
        EXPECT_EQ(parentPtr + 1, childPtr);
    }

    // ========== Edge Cases & Stress Tests ==========

    /// @brief Verifies that the pool handles entities with large ID values correctly.
    TEST(EcsTransformNodePool, Add_EntityWithLargeIdValue_HandledCorrectly)
    {
        EcsComponentPool<TransformNode2> pool;
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

        pool.Add(largeId, {});

        EXPECT_TRUE(pool.Has(largeId));
        EXPECT_EQ(pool.size(), 1);
    }

    /// @brief Verifies that a sequence of add and remove operations maintains pool integrity.
    TEST(EcsTransformNodePool, AddRemove_SequenceOfOperations_IntegrityMaintained)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        TrsTransformf localTransforms[10] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        std::vector<EntityId> ids;
        for (int i = 0; i < std::size(localTransforms); ++i)
        {
            ids.push_back(idGen.NextId());
            pool.Add(ids.back(), TransformNode2{ .LocalTransform = localTransforms[i] });
        }
        
        EXPECT_EQ(pool.size(), 10);

        // Remove every other one
        for (size_t i = 0; i < ids.size(); i += 2)
        {
            pool.Remove(ids[i]);
        }
        EXPECT_EQ(pool.size(), 5);

        // Verify remaining ones are still accessible
        for (size_t i = 1; i < ids.size(); i += 2)
        {
            EXPECT_TRUE(pool.Has(ids[i]));
            EXPECT_EQ(pool[ids[i]].LocalTransform, localTransforms[i]);
        }

        // Add them back
        for (size_t i = 0; i < ids.size(); i += 2)
        {
            pool.Add(ids[i], TransformNode2{ .LocalTransform = localTransforms[i] } );
        }
        EXPECT_EQ(pool.size(), 10);

        for(size_t i = 0; i < ids.size(); ++i)
        {
            EXPECT_TRUE(pool.Has(ids[i]));
            EXPECT_EQ(pool[ids[i]].LocalTransform, localTransforms[i]);
        }
    }

    /// @brief Verifies that the pool handles sparse entity IDs efficiently with appropriate index growth.
    TEST(EcsTransformNodePool, Add_SparseEntityIds_IndexGrowsAppropriately)
    {
        EcsComponentPool<TransformNode2> pool;
        EcsRegistry registry;

        // Create sparse IDs by creating and destroying intermediate ones
        const EntityId id1 = registry.Create();
        
        for (int i = 0; i < 50; ++i)
        {
            // Satisfy [[nodiscard]] warning
            auto tempId = registry.Create();
        }
        
        const EntityId id2 = registry.Create();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(id1, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(id2, TransformNode2{ .LocalTransform = localTransforms[1] });

        EXPECT_EQ(pool.size(), 2);
        EXPECT_TRUE(pool.Has(id1));
        EXPECT_TRUE(pool.Has(id2));

        EXPECT_EQ(pool[id1].LocalTransform, localTransforms[0]);
        EXPECT_EQ(pool[id2].LocalTransform, localTransforms[1]);
    }

    // ========== Complex Scenario Tests ==========

    /// @brief Verifies that multiple independent hierarchies can coexist and be removed independently.
    TEST(EcsTransformNodePool, AddRemove_MultipleIndependentHierarchies_IndependentManagement)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        // Create two separate hierarchies
        const EntityId root1 = idGen.NextId();
        const EntityId root1_child1 = idGen.NextId();
        const EntityId root1_child2 = idGen.NextId();

        const EntityId root2 = idGen.NextId();
        const EntityId root2_child1 = idGen.NextId();
        const EntityId root2_child2 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(root1, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(root1_child1, TransformNode2{ root1, localTransforms[1] });
        pool.Add(root1_child2, TransformNode2{ root1, localTransforms[2] });

        pool.Add(root2, TransformNode2{ .LocalTransform = localTransforms[3] });
        pool.Add(root2_child1, TransformNode2{ root2, localTransforms[4] });
        pool.Add(root2_child2, TransformNode2{ root2, localTransforms[5] });
        EXPECT_EQ(pool.size(), 6);

        // Remove first hierarchy
        pool.Remove(root1);

        EXPECT_EQ(pool.size(), 3);
        EXPECT_FALSE(pool.Has(root1));
        EXPECT_FALSE(pool.Has(root1_child1));
        EXPECT_FALSE(pool.Has(root1_child2));
        EXPECT_TRUE(pool.Has(root2));
        EXPECT_TRUE(pool.Has(root2_child1));
        EXPECT_TRUE(pool.Has(root2_child2));

        // Verify second hierarchy intact
        EXPECT_EQ(pool[root2].LocalTransform, localTransforms[3]);
        EXPECT_EQ(pool[root2_child1].ParentId, root2);
        EXPECT_EQ(pool[root2_child1].LocalTransform, localTransforms[4]);
        EXPECT_EQ(pool[root2_child2].ParentId, root2);
        EXPECT_EQ(pool[root2_child2].LocalTransform, localTransforms[5]);
    }

    /// @brief Verifies that adding grandchildren after siblings maintains correct hierarchical ordering.
    TEST(EcsTransformNodePool, Add_GrandchildrenAfterSiblings_CorrectOrderingMaintained)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        const EntityId root = idGen.NextId();
        const EntityId child1 = idGen.NextId();
        const EntityId child2 = idGen.NextId();
        const EntityId child3 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(root, TransformNode2{ .LocalTransform = localTransforms[0] });
        
        // Add children, but later add grandchildren to first child
        // Note the order in the pool is the reverse of addition order
        pool.Add(child1, TransformNode2{ root, localTransforms[1] });
        pool.Add(child2, TransformNode2{ root, localTransforms[2] });
        pool.Add(child3, TransformNode2{ root, localTransforms[3] });

        // Now add grandchild to child2 (should insert after child2)
        const EntityId grandchild1 = idGen.NextId();
        pool.Add(grandchild1, TransformNode2{ child2, localTransforms[4] });
        // Expected order: root, child3, child2, grandchild1, child1
        auto it = pool.begin();
        EXPECT_EQ(*it, root);
        EXPECT_EQ(pool[*it].LocalTransform, localTransforms[0]);
         ++it;
        EXPECT_EQ(*it, child3);
        EXPECT_EQ(pool[*it].LocalTransform, localTransforms[3]);
         ++it;
        EXPECT_EQ(*it, child2);
        EXPECT_EQ(pool[*it].LocalTransform, localTransforms[2]);
         ++it;
        EXPECT_EQ(*it, grandchild1);
        EXPECT_EQ(pool[*it].LocalTransform, localTransforms[4]);
         ++it;
        EXPECT_EQ(*it, child1);
        EXPECT_EQ(pool[*it].LocalTransform, localTransforms[1]);

        // Verify contiguity
        TransformNode2* ptrs[] =
        {
            &pool[root],
            &pool[child3],
            &pool[child2],
            &pool[grandchild1],
            &pool[child1]
        };
        for (size_t i = 0; i < std::size(ptrs) - 1; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
        }
    }

    /// @brief Verifies that removing a single leaf from a complex hierarchy preserves the remaining structure.
    TEST(EcsTransformNodePool, Remove_SingleLeafFromComplexHierarchy_RemainingStructurePreserved)
    {
        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;

        // Build complex tree
        const EntityId root = idGen.NextId();
        const EntityId branch1 = idGen.NextId();
        const EntityId branch2 = idGen.NextId();
        const EntityId leaf1_1 = idGen.NextId();
        const EntityId leaf1_2 = idGen.NextId();
        const EntityId leaf2_1 = idGen.NextId();

        TrsTransformf localTransforms[] =
        {
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform(),
            RandomTrsTransform()
        };

        pool.Add(root, TransformNode2{ .LocalTransform = localTransforms[0] });
        pool.Add(branch1, TransformNode2{ root, localTransforms[1] });
        pool.Add(leaf1_1, TransformNode2{ branch1, localTransforms[2] });
        pool.Add(leaf1_2, TransformNode2{ branch1, localTransforms[3] });
        pool.Add(branch2, TransformNode2{ root, localTransforms[4] });
        pool.Add(leaf2_1, TransformNode2{ branch2, localTransforms[5] });
        // Remove one leaf
        pool.Remove(leaf1_1);

        EXPECT_EQ(pool.size(), 5);
        EXPECT_FALSE(pool.Has(leaf1_1));
        
        // Verify structure is preserved
        EXPECT_TRUE(pool.Has(root));
        EXPECT_TRUE(pool.Has(branch1));
        EXPECT_TRUE(pool.Has(leaf1_2));
        EXPECT_TRUE(pool.Has(branch2));
        EXPECT_TRUE(pool.Has(leaf2_1));

        // Verify relationships
        EXPECT_EQ(pool[leaf1_2].ParentId, branch1);
        EXPECT_EQ(pool[leaf2_1].ParentId, branch2);

        EXPECT_EQ(pool[root].LocalTransform, localTransforms[0]);
        EXPECT_EQ(pool[branch1].LocalTransform, localTransforms[1]);
        EXPECT_EQ(pool[leaf1_2].LocalTransform, localTransforms[3]);
        EXPECT_EQ(pool[branch2].LocalTransform, localTransforms[4]);
        EXPECT_EQ(pool[leaf2_1].LocalTransform, localTransforms[5]);

        // Verify ordering and contiguity
        TransformNode2* ptrs[] =
        {
            &pool[root],
            &pool[branch2],
            &pool[leaf2_1],
            &pool[branch1],
            &pool[leaf1_2]
        };
        for (size_t i = 0; i < std::size(ptrs) - 1; ++i)
        {
            EXPECT_EQ(ptrs[i] + 1, ptrs[i + 1]);
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
    static void AddNewHierarchy(EcsComponentPool<TransformNode2>& pool, TestIdGenerator& idGen, std::vector<HierarchyInfo>& hierarchies)
    {
        HierarchyInfo hierarchy;
        hierarchy.root = idGen.NextId();
        pool.Add(hierarchy.root, {});

        // Add 1-5 children
        const int numChildren = (rand() % 5) + 1;
        for (int i = 0; i < numChildren; ++i)
        {
            EntityId child = idGen.NextId();
            pool.Add(child, TransformNode2{ hierarchy.root, RandomTrsTransform() });
            hierarchy.children.push_back(child);

            // 50% chance to add grandchildren to this child
            if (rand() % 2 == 0)
            {
                const int numGrandchildren = (rand() % 3) + 1;
                for (int j = 0; j < numGrandchildren; ++j)
                {
                    EntityId grandchild = idGen.NextId();
                    pool.Add(grandchild, TransformNode2{ child, RandomTrsTransform() });
                    hierarchy.grandchildren.push_back(grandchild);
                }
            }
        }

        hierarchies.push_back(hierarchy);
    }

    /// @brief Adds nodes to an existing hierarchy (either children or grandchildren).
    static void AddToExistingHierarchy(EcsComponentPool<TransformNode2>& pool, TestIdGenerator& idGen, std::vector<HierarchyInfo>& hierarchies)
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
            pool.Add(grandchild, TransformNode2{ hierarchy.children[childIdx], RandomTrsTransform() });
            hierarchy.grandchildren.push_back(grandchild);
        }
        else
        {
            // Add a new child to the root
            EntityId child = idGen.NextId();
            pool.Add(child, TransformNode2{ hierarchy.root, RandomTrsTransform() });
            hierarchy.children.push_back(child);
        }
    }

    /// @brief Removes an entire hierarchy and verifies all nodes are removed.
    static void RemoveEntireHierarchy(EcsComponentPool<TransformNode2>& pool, std::vector<HierarchyInfo>& hierarchies)
    {
        if (hierarchies.empty())
        {
            return;
        }

        const size_t hierarchyIdx = rand() % hierarchies.size();
        const auto& hierarchy = hierarchies[hierarchyIdx];

        // Verify hierarchy exists before removal
        EXPECT_TRUE(pool.Has(hierarchy.root));

        pool.Remove(hierarchy.root);

        // Verify entire hierarchy is removed
        EXPECT_FALSE(pool.Has(hierarchy.root));
        for (const auto& child : hierarchy.children)
        {
            EXPECT_FALSE(pool.Has(child));
        }
        for (const auto& grandchild : hierarchy.grandchildren)
        {
            EXPECT_FALSE(pool.Has(grandchild));
        }

        // Remove from tracking
        hierarchies.erase(hierarchies.begin() + hierarchyIdx);
    }

    /// @brief Removes a partial hierarchy (middle node with its descendants).
    static void RemovePartialHierarchy(EcsComponentPool<TransformNode2>& pool, std::vector<HierarchyInfo>& hierarchies)
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

            pool.Remove(childToRemove);

            // Verify child is removed
            EXPECT_FALSE(pool.Has(childToRemove));

            // Remove from tracking
            hierarchy.children.erase(hierarchy.children.begin() + childIdx);

            // Remove associated grandchildren from tracking
            hierarchy.grandchildren.erase(
                std::remove_if(hierarchy.grandchildren.begin(), hierarchy.grandchildren.end(),
                    [&](EntityId id) { return !pool.Has(id); }),
                hierarchy.grandchildren.end());

            // If hierarchy has no children left, remove it from tracking
            if (hierarchy.children.empty())
            {
                pool.Remove(hierarchy.root);
                hierarchies.erase(hierarchies.begin() + hierarchyIdx);
            }
        }
    }

    /// @brief Adds standalone top-level nodes and randomly removes half of them.
    static void AddAndRemoveStandaloneNodes(EcsComponentPool<TransformNode2>& pool, TestIdGenerator& idGen)
    {
        const int numNodes = (rand() % 5) + 1;
        for (int i = 0; i < numNodes; ++i)
        {
            EntityId standalone = idGen.NextId();
            pool.Add(standalone, {});
            
            // Immediately remove half of them (stress test)
            if (rand() % 2 == 0)
            {
                pool.Remove(standalone);
            }
        }
    }

    /// @brief Verifies pool integrity including hierarchy relationships and memory contiguity.
    static void VerifypoolIntegrity(const EcsComponentPool<TransformNode2>& pool, const std::vector<HierarchyInfo>& hierarchies, int iteration)
    {
        // Verify all tracked hierarchies still exist correctly
        for (const auto& hierarchy : hierarchies)
        {
            EXPECT_TRUE(pool.Has(hierarchy.root)) 
                << "Iteration " << iteration << ": Root should exist";
            
            for (const auto& child : hierarchy.children)
            {
                EXPECT_TRUE(pool.Has(child)) 
                    << "Iteration " << iteration << ": Child should exist";
                EXPECT_EQ(pool[child].ParentId, hierarchy.root)
                    << "Iteration " << iteration << ": Child should have correct parent";
            }
        }

        // Verify memory contiguity
        if (pool.size() > 1)
        {
            auto it = pool.begin();
            const EntityId* prevEid = &(*it);
            ++it;
            
            for (; it != pool.end(); ++it)
            {
                const EntityId* currentEid = &(*it);
                EXPECT_EQ(currentEid, prevEid + 1) 
                    << "Iteration " << iteration << ": Nodes not contiguous in memory";
                prevEid = currentEid;
            }
        }
    }

    /// @brief Performs final verification of all remaining hierarchies and memory contiguity.
    static void VerifyFinalState(const EcsComponentPool<TransformNode2>& pool, const std::vector<HierarchyInfo>& hierarchies)
    {
        // Verify all remaining hierarchies
        for (const auto& hierarchy : hierarchies)
        {
            EXPECT_TRUE(pool.Has(hierarchy.root)) << "Final: Root should exist";
            
            const TransformNode2& rootNode = pool[hierarchy.root];
            EXPECT_FALSE(rootNode.ParentId.IsValid()) << "Final: Root should have no parent";

            for (const auto& child : hierarchy.children)
            {
                EXPECT_TRUE(pool.Has(child)) << "Final: Child should exist";
                const TransformNode2& childNode = pool[child];
                EXPECT_EQ(childNode.ParentId, hierarchy.root) << "Final: Child should have correct parent";
            }

            for (const auto& grandchild : hierarchy.grandchildren)
            {
                EXPECT_TRUE(pool.Has(grandchild)) << "Final: Grandchild should exist";
            }
        }

        // Verify final memory contiguity
        if (pool.size() > 0)
        {
            auto it = pool.begin();
            const EntityId* firstEid = &(*it);
            
            size_t index = 0;
            for (const auto& eid : pool)
            {
                const EntityId* expectedAddress = firstEid + index;
                const EntityId* actualAddress = &eid;
                
                EXPECT_EQ(actualAddress, expectedAddress) 
                    << "Final: Node at index " << index << " not contiguous";
                
                ++index;
            }
        }
    }

    /// @brief Initializes a pool with a specified number of items in random hierarchies.
    static void InitializepoolWithRandomHierarchies(EcsComponentPool<TransformNode2>& pool, TestIdGenerator& idGen, 
                                                          std::vector<HierarchyInfo>& activeHierarchies, size_t targetItemCount)
    {
        std::cout << "Initializing pool with " << targetItemCount << " items...\n";
        size_t totalItemsAdded = 0;
        
        while (totalItemsAdded < targetItemCount)
        {
            const int hierarchyType = rand() % 10;
            
            if (hierarchyType < 2)
            {
                // Add a standalone top-level node (20% chance)
                EntityId standalone = idGen.NextId();
                pool.Add(standalone, {});
                totalItemsAdded++;
            }
            else if (hierarchyType < 5)
            {
                // Add a simple 2-level hierarchy: 1 root + 1-3 children (30% chance)
                HierarchyInfo hierarchy;
                hierarchy.root = idGen.NextId();
                pool.Add(hierarchy.root, {});
                totalItemsAdded++;

                const int numChildren = (rand() % 3) + 1;
                for (int i = 0; i < numChildren && totalItemsAdded < targetItemCount; ++i)
                {
                    EntityId child = idGen.NextId();
                    pool.Add(child, TransformNode2{ hierarchy.root, RandomTrsTransform() });
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
                pool.Add(hierarchy.root, {});
                totalItemsAdded++;

                const int numChildren = (rand() % 3) + 2;
                for (int i = 0; i < numChildren && totalItemsAdded < targetItemCount; ++i)
                {
                    EntityId child = idGen.NextId();
                    pool.Add(child, TransformNode2{ hierarchy.root, RandomTrsTransform() });
                    hierarchy.children.push_back(child);
                    totalItemsAdded++;

                    // 60% chance to add 1-2 grandchildren
                    if (rand() % 10 < 6)
                    {
                        const int numGrandchildren = (rand() % 2) + 1;
                        for (int j = 0; j < numGrandchildren && totalItemsAdded < targetItemCount; ++j)
                        {
                            EntityId grandchild = idGen.NextId();
                            pool.Add(grandchild, TransformNode2{ child, RandomTrsTransform() });
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
                pool.Add(hierarchy.root, {});
                totalItemsAdded++;

                const int numChildren = (rand() % 4) + 3;
                for (int i = 0; i < numChildren && totalItemsAdded < targetItemCount; ++i)
                {
                    EntityId child = idGen.NextId();
                    pool.Add(child, TransformNode2{ hierarchy.root, RandomTrsTransform() });
                    hierarchy.children.push_back(child);
                    totalItemsAdded++;

                    // Add 2-5 grandchildren to each child
                    const int numGrandchildren = (rand() % 4) + 2;
                    for (int j = 0; j < numGrandchildren && totalItemsAdded < targetItemCount; ++j)
                    {
                        EntityId grandchild = idGen.NextId();
                        pool.Add(grandchild, TransformNode2{ child, RandomTrsTransform() });
                        hierarchy.grandchildren.push_back(grandchild);
                        totalItemsAdded++;

                        // 40% chance to add great-grandchildren (4th level)
                        if (rand() % 10 < 4)
                        {
                            const int numGreatGrandchildren = (rand() % 3) + 1;
                            for (int k = 0; k < numGreatGrandchildren && totalItemsAdded < targetItemCount; ++k)
                            {
                                EntityId greatGrandchild = idGen.NextId();
                                pool.Add(greatGrandchild, TransformNode2{ grandchild, RandomTrsTransform() });
                                totalItemsAdded++;
                            }
                        }
                    }
                }

                activeHierarchies.push_back(hierarchy);
            }
        }

        std::cout << "Initialization complete. pool size: " << pool.size() 
                  << ", Active hierarchies: " << activeHierarchies.size() << "\n";
        
        EXPECT_EQ(pool.size(), targetItemCount) 
            << "pool should be initialized with exactly " << targetItemCount << " items";

        // Verify initial memory contiguity
        if (pool.size() > 1)
        {
            auto it = pool.begin();
            const EntityId* firstEid = &(*it);
            
            size_t index = 0;
            for (const auto& eid : pool)
            {
                const EntityId* expectedAddress = firstEid + index;
                const EntityId* actualAddress = &eid;
                
                EXPECT_EQ(actualAddress, expectedAddress) 
                    << "Initial: Node at index " << index << " not contiguous in memory";
                
                ++index;
            }
        }
    }

    /// @brief Stress test that performs many add/remove operations on a large pool with complex hierarchies.
    TEST(EcsTransformNodePool, StressTest_ManyOperationsOnLargepool_IntegrityMaintained)
    {
        // Seed RNG with current time for randomness, but log it for reproducibility
        const unsigned int seed = static_cast<unsigned int>(std::time(nullptr));
        std::srand(seed);
        std::cout << "Stress test RNG seed: " << seed << " (use this seed to reproduce the test sequence)\n";

        EcsComponentPool<TransformNode2> pool;
        TestIdGenerator idGen;
        std::vector<HierarchyInfo> activeHierarchies;

        const int NUM_ITERATIONS = 1000;
        const int MAX_HIERARCHIES = 50;

        // Initialize pool with 10,000 items in random hierarchies
        InitializepoolWithRandomHierarchies(pool, idGen, activeHierarchies, 10000);

        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration)
        {
            const int operation = rand() % 10; // 0-9 for different operations

            if (operation < 3 && activeHierarchies.size() < MAX_HIERARCHIES)
            {
                // Add a new multi-level hierarchy (30% chance)
                AddNewHierarchy(pool, idGen, activeHierarchies);
            }
            else if (operation < 5)
            {
                // Add nodes to an existing hierarchy (20% chance)
                AddToExistingHierarchy(pool, idGen, activeHierarchies);
            }
            else if (operation < 7)
            {
                // Remove an entire hierarchy (20% chance)
                RemoveEntireHierarchy(pool, activeHierarchies);
            }
            else if (operation < 9)
            {
                // Remove a partial hierarchy (middle node) (20% chance)
                RemovePartialHierarchy(pool, activeHierarchies);
            }
            else
            {
                // Add some standalone top-level nodes (10% chance)
                AddAndRemoveStandaloneNodes(pool, idGen);
            }

            // Periodically verify pool integrity
            if (iteration % 10 == 0)
            {
                VerifypoolIntegrity(pool, activeHierarchies, iteration);
            }
        }

        // Final verification
        VerifyFinalState(pool, activeHierarchies);
    }
}
