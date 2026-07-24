#include <gtest/gtest.h>

#include "BoundingVolumes.h"
#include "GridHash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <random>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
	BoundingSphere MakeSphere(float radius)
	{
		return BoundingSphere{ Vec3f{0}, radius };
	}

	BoundingSphere MakeSphere()
	{
		return MakeSphere(0.1f);
	}

	BoundingSphere MakeSphere(const Vec3f& center, const float radius)
	{
		return BoundingSphere{ center, radius };
	}
}

TEST(UniqueBodyPairSet, GrowsAndRetainsItems)
{
	UniqueBodyPairSet set{16};

	for(uint64_t item = 0; item < 40; ++item)
	{
		EXPECT_TRUE(set.Insert(item));
	}

	EXPECT_EQ(set.Size(), 40u);

	for(uint64_t item = 0; item < 40; ++item)
	{
		EXPECT_TRUE(set.Contains(item));
		EXPECT_FALSE(set.Insert(item));
	}

	EXPECT_EQ(set.Size(), 40u);

	set.Clear();

	EXPECT_EQ(set.Size(), 0u);

	for(uint64_t item = 0; item < 40; ++item)
	{
		EXPECT_FALSE(set.Contains(item));
		EXPECT_TRUE(set.Insert(item));
	}

	EXPECT_EQ(set.Size(), 40u);
}

TEST(GridHash, EmptyGridHasNoPotentialCollisions)
{
	GridHash hash{3};

    const std::span pairs(hash);

	EXPECT_TRUE(pairs.empty());
}

TEST(GridHash, ReportsConfiguredCellSize)
{
	const GridHash hash{17};

	EXPECT_EQ(hash.GetCellSize(), 17u);
}

TEST(GridHash, PotentialCollisionCountAndIteratorsAreStable)
{
	GridHash hash{3};

	hash.Add(Vec3f{0.1f}, Vec3f{0.9f}, MakeSphere(), 5);
	hash.Add(Vec3f{0.2f}, Vec3f{0.8f}, MakeSphere(), 2);

	EXPECT_EQ(hash.PotentialCollisionCount(), 1u);
	EXPECT_EQ(hash.PotentialCollisionCount(), 1u);
	ASSERT_NE(hash.begin(), hash.end());
	EXPECT_EQ(*hash.begin(), BodyPair(2, 5));
	EXPECT_EQ(static_cast<size_t>(std::distance(hash.begin(), hash.end())), 1u);
}

TEST(GridHash, NegativeCoordinatesAreQuantizedUsingFloor)
{
	GridHash hash{3};

	hash.Add(Vec3f{-0.1f}, Vec3f{-0.1f}, MakeSphere(0.001f), 0);
	hash.Add(Vec3f{-2.9f}, Vec3f{-2.9f}, MakeSphere(0.001f), 1);
	hash.Add(Vec3f{0.1f}, Vec3f{0.1f}, MakeSphere(0.001f), 2);

	const std::vector<BodyPair> pairs(hash.begin(), hash.end());

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
}

TEST(GridHash, BoundingSphereCenterAndRadiusExpandOccupiedCells)
{
	GridHash hash{3};

	// The first body's local sphere center moves its occupied cell from the origin to x=3.
	hash.Add(Vec3f{0.0f}, Vec3f{0.0f}, MakeSphere(Vec3f{3.1f, 0.0f, 0.0f}, 0.001f), 0);
	hash.Add(Vec3f{3.1f, 0.0f, 0.0f}, Vec3f{3.1f, 0.0f, 0.0f}, MakeSphere(0.001f), 1);

	// Radius makes this body cross the x=3 cell boundary and share the same cell as body 1.
	hash.Add(Vec3f{2.9f, 0.0f, 0.0f}, Vec3f{2.9f, 0.0f, 0.0f}, MakeSphere(0.2f), 2);

	std::vector<BodyPair> pairs(hash.begin(), hash.end());
	std::ranges::sort(pairs);

	const std::vector<BodyPair> expected{
		BodyPair(0, 1),
		BodyPair(0, 2),
		BodyPair(1, 2),
	};
	EXPECT_EQ(pairs, expected);
}

TEST(GridHash, SingleBodyProducesNoPairs)
{
	GridHash hash{3};

	hash.Add(
		Vec3f{ 0.8f, 0.8f, 0.8f },
		Vec3f{ 0.2f, 0.2f, 0.2f },
		MakeSphere(),
		3);

    const std::span pairs(hash);
	EXPECT_TRUE(pairs.empty());
}

TEST(GridHash, TwoBodiesInSameCellProduceOneOrderedPair)
{
	GridHash hash{3};

	hash.Add(
		Vec3f{ 0.9f, 0.9f, 0.9f },
		Vec3f{ 0.1f, 0.1f, 0.1f },
		MakeSphere(),
		7);

	hash.Add(
		Vec3f{ 1.8f, 1.8f, 1.8f },
		Vec3f{ 1.0f, 1.0f, 1.0f },
		MakeSphere(),
		3);

    const std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(3, 7));
}

TEST(GridHash, SharedAcrossManyCellsStillProducesUniquePair)
{
	GridHash hash{3};

	hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 9.9f, 9.9f, 0.2f },
		MakeSphere(),
		0);

	hash.Add(
		Vec3f{ 0.2f, 0.2f, 0.1f },
		Vec3f{ 9.8f, 9.8f, 0.2f },
		MakeSphere(),
		1);

    const std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
}

TEST(GridHash, ThreeBodiesInOneCellGenerateAllUniquePairs)
{
	GridHash hash{3};

	hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.9f, 0.9f, 0.9f },
		MakeSphere(),
		0);
	hash.Add(
		Vec3f{ 0.7f, 0.7f, 0.7f },
		Vec3f{ 0.3f, 0.3f, 0.3f },
		MakeSphere(),
		1);
	hash.Add(
		Vec3f{ 0.4f, 0.4f, 0.4f },
		Vec3f{ 0.6f, 0.6f, 0.6f },
		MakeSphere(),
		2);

    const std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 3u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
	EXPECT_EQ(pairs[1], BodyPair(0, 2));
	EXPECT_EQ(pairs[2], BodyPair(1, 2));
}

TEST(GridHash, ClearRemovesExistingCellsAndPairs)
{
	GridHash hash{3};

	hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.9f, 0.9f, 0.9f },
		MakeSphere(),
		0);
	hash.Add(
		Vec3f{ 0.2f, 0.2f, 0.2f },
		Vec3f{ 0.8f, 0.8f, 0.8f },
		MakeSphere(),
		1);

    const std::span beforeClear(hash);
	ASSERT_EQ(beforeClear.size(), 1u);

	hash.Clear();

	const std::span afterClear(hash);
	EXPECT_TRUE(afterClear.empty());

	hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.2f, 0.2f, 0.2f },
		MakeSphere(),
		0);
	hash.Add(
		Vec3f{ 20.2f, 20.2f, 20.2f },
		Vec3f{ 20.1f, 20.1f, 20.1f },
		MakeSphere(),
		1);

	const std::span nonOverlappingPairs(hash);
	EXPECT_TRUE(nonOverlappingPairs.empty());
}

TEST(GridHash, ClearAllowsTheSamePairToBeGeneratedAgain)
{
	GridHash hash{3};

	const auto addPair = [&hash]()
	{
		hash.Add(Vec3f{0.1f}, Vec3f{0.9f}, MakeSphere(), 0);
		hash.Add(Vec3f{0.2f}, Vec3f{0.8f}, MakeSphere(), 1);
	};

	addPair();
	ASSERT_EQ(hash.PotentialCollisionCount(), 1u);

	hash.Clear();
	addPair();

	ASSERT_EQ(hash.PotentialCollisionCount(), 1u);
	EXPECT_EQ(*hash.begin(), BodyPair(0, 1));
}

TEST(GridHash, GrowsUniquePairStorage)
{
	GridHash hash{3};
	constexpr uint32_t kBodyCount = 47;

	for(uint32_t bodyIndex = 0; bodyIndex < kBodyCount; ++bodyIndex)
	{
		hash.Add(Vec3f{0.1f}, Vec3f{0.9f}, MakeSphere(), bodyIndex);
	}

	const size_t expectedCount =
		(static_cast<size_t>(kBodyCount) * (kBodyCount - 1)) / 2;
	ASSERT_GT(expectedCount, 1024u);
	ASSERT_EQ(hash.PotentialCollisionCount(), expectedCount);

	std::vector<BodyPair> actual(hash.begin(), hash.end());
	std::vector<BodyPair> expected;
	expected.reserve(expectedCount);
	for(uint32_t indexA = 0; indexA < kBodyCount; ++indexA)
	{
		for(uint32_t indexB = indexA + 1; indexB < kBodyCount; ++indexB)
		{
			expected.emplace_back(indexA, indexB);
		}
	}

	std::ranges::sort(actual);
	std::ranges::sort(expected);
	EXPECT_EQ(actual, expected);
}

TEST(GridHash, MoveConstructionAndAssignmentPreservePairs)
{
	GridHash source{3};
	source.Add(Vec3f{0.1f}, Vec3f{0.9f}, MakeSphere(), 4);
	source.Add(Vec3f{0.2f}, Vec3f{0.8f}, MakeSphere(), 9);

	GridHash moveConstructed{std::move(source)};
	ASSERT_EQ(moveConstructed.PotentialCollisionCount(), 1u);
	EXPECT_EQ(*moveConstructed.begin(), BodyPair(4, 9));

	GridHash moveAssigned{7};
	moveAssigned = std::move(moveConstructed);

	EXPECT_EQ(moveAssigned.GetCellSize(), 3u);
	ASSERT_EQ(moveAssigned.PotentialCollisionCount(), 1u);
	EXPECT_EQ(*moveAssigned.begin(), BodyPair(4, 9));
}

TEST(GridHash, GridHash4AndGridHash5ContainSameBodyPairSet)
{
	GridHash hash2{2};
	GridHash hash3{3};

	struct BodyInput
	{
		Vec3f Min;
		Vec3f Max;
		float Radius;
		uint32_t BodyIndex;
	};

	const std::vector<BodyInput> bodies//
	{
		{ .Min{ 0.0f, 0.0f, 0.0f }, .Max{ 2.0f, 2.0f, 2.0f }, .Radius = 0.25f, .BodyIndex = 0 },
		{ .Min{ 1.0f, 1.0f, 1.0f }, .Max{ 3.0f, 3.0f, 3.0f }, .Radius = 0.25f, .BodyIndex = 1 },
		{ .Min{ 10.0f, 0.0f, 0.0f }, .Max{ 12.0f, 2.0f, 2.0f }, .Radius = 0.25f, .BodyIndex = 2 },
		{ .Min{ 11.0f, 1.0f, 1.0f }, .Max{ 13.0f, 3.0f, 3.0f }, .Radius = 0.25f, .BodyIndex = 3 },
		{ .Min{ 50.0f, 50.0f, 50.0f }, .Max{ 52.0f, 52.0f, 52.0f }, .Radius = 0.25f, .BodyIndex = 4 },
	};

	for(const BodyInput& body : bodies)
	{
		hash2.Add(body.Min, body.Max, MakeSphere(body.Radius), body.BodyIndex);
		hash3.Add(body.Min, body.Max, MakeSphere(body.Radius), body.BodyIndex);
	}

	std::vector<BodyPair> pairs2(hash2.begin(), hash2.end());
	std::vector<BodyPair> pairs3(hash3.begin(), hash3.end());

	std::ranges::sort(pairs2);
	std::ranges::sort(pairs3);

	EXPECT_EQ(pairs2, pairs3);
}

TEST(GridHash, ChaosRandomizedBodies_AllExpectedPairsExist)
{
	struct RandomBody
	{
		Vec3f Min;
		Vec3f Max;
		float Radius;
		std::array<int64_t, 3> MinQ;
		std::array<int64_t, 3> MaxQ;
	};

	constexpr uint32_t kBodyCount = 1000;

    std::mt19937 rng(0xC0FFEEu);

    for(size_t cellSize = 1; cellSize <= 10; ++cellSize)
    {
        GridHash hash{cellSize};

        const float cellSizeF = static_cast<float>(hash.GetCellSize());

        const auto quantize = [cellSizeF](const float value)
        {
            return static_cast<int64_t>(std::floor(value / cellSizeF));
        };

        std::uniform_real_distribution<float> centerDistXZ(-400.0f, 400.0f);
        std::uniform_real_distribution<float> centerDistY(-64.0f, 64.0f);
        std::uniform_real_distribution<float> halfExtentDist(0.05f, 3.0f);
        std::uniform_real_distribution<float> radiusDist(0.01f, 1.25f);

        std::vector<RandomBody> bodies;
        bodies.reserve(kBodyCount);

        for(uint32_t i = 0; i < kBodyCount; ++i)
        {
            const Vec3f center //
                {
                    centerDistXZ(rng),
                    centerDistY(rng),
                    centerDistXZ(rng),
                };

            const Vec3f halfExtent //
                {
                    halfExtentDist(rng),
                    halfExtentDist(rng),
                    halfExtentDist(rng),
                };

            const Vec3f bbMin = center - halfExtent;
            const Vec3f bbMax = center + halfExtent;
            const float radius = radiusDist(rng);

            const RandomBody body //
                {
                    .Min = bbMin,
                    .Max = bbMax,
                    .Radius = radius,
                    .MinQ //
                    {
                        quantize(bbMin.x - radius),
                        quantize(bbMin.y - radius),
                        quantize(bbMin.z - radius),
                    },
                    .MaxQ //
                    {
                        quantize(bbMax.x + radius),
                        quantize(bbMax.y + radius),
                        quantize(bbMax.z + radius),
                    },
                };

            hash.Add(body.Min, body.Max, MakeSphere(body.Radius), i);
            bodies.push_back(body);
        }

        std::vector<BodyPair> expectedPairs;

        for(size_t i = 0; i < bodies.size(); ++i)
        {
            for(size_t j = i + 1; j < bodies.size(); ++j)
            {
                const bool xOverlap = bodies[i].MinQ[0] <= bodies[j].MaxQ[0]
                    && bodies[j].MinQ[0] <= bodies[i].MaxQ[0];
                const bool yOverlap = bodies[i].MinQ[1] <= bodies[j].MaxQ[1]
                    && bodies[j].MinQ[1] <= bodies[i].MaxQ[1];
                const bool zOverlap = bodies[i].MinQ[2] <= bodies[j].MaxQ[2]
                    && bodies[j].MinQ[2] <= bodies[i].MaxQ[2];

                if(xOverlap && yOverlap && zOverlap)
                {
                    expectedPairs.emplace_back(i, j);
                }
            }
        }

        ASSERT_FALSE(expectedPairs.empty());

        std::vector<BodyPair> actualPairs(hash.begin(), hash.end());

        ASSERT_EQ(expectedPairs.size(), actualPairs.size());

        std::ranges::sort(expectedPairs);
        std::ranges::sort(actualPairs);

        for(size_t i = 0; i < expectedPairs.size(); ++i)
        {
            EXPECT_EQ(expectedPairs[i], actualPairs[i]);
        }
    }
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
