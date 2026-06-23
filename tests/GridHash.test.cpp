#include <gtest/gtest.h>

#include "PhysicsLevel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
	Collider MakeSphereCollider(float radius)
	{
		return Collider{ BoundingSphere{ Vec3f{ 0, 0, 0 }, radius } };
	}

	Collider MakeSphereCollider()
	{
		return MakeSphereCollider(0.1f);
	}
}

TEST(GridHash, EmptyGridHasNoPotentialCollisions)
{
	GridHash hash{3};

    const std::span pairs(hash);

	EXPECT_TRUE(pairs.empty());
}

TEST(GridHash, SingleBodyProducesNoPairs)
{
	GridHash hash{3};

	const Result<> result = hash.Add(
		Vec3f{ 0.8f, 0.8f, 0.8f },
		Vec3f{ 0.2f, 0.2f, 0.2f },
		MakeSphereCollider(),
		3);

	ASSERT_TRUE(result);

    const std::span pairs(hash);
	EXPECT_TRUE(pairs.empty());
}

TEST(GridHash, TwoBodiesInSameCellProduceOneOrderedPair)
{
	GridHash hash{3};

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.9f, 0.9f, 0.9f },
		Vec3f{ 0.1f, 0.1f, 0.1f },
		MakeSphereCollider(),
		7));

	ASSERT_TRUE(hash.Add(
		Vec3f{ 1.8f, 1.8f, 1.8f },
		Vec3f{ 1.0f, 1.0f, 1.0f },
		MakeSphereCollider(),
		3));

    const std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(3, 7));
}

TEST(GridHash, SharedAcrossManyCellsStillProducesUniquePair)
{
	GridHash hash{3};

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 9.9f, 9.9f, 0.2f },
		MakeSphereCollider(),
		0));

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.2f, 0.2f, 0.1f },
		Vec3f{ 9.8f, 9.8f, 0.2f },
		MakeSphereCollider(),
		1));

    const std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
}

TEST(GridHash, ThreeBodiesInOneCellGenerateAllUniquePairs)
{
	GridHash hash{3};

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.9f, 0.9f, 0.9f },
		MakeSphereCollider(),
		0));
	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.7f, 0.7f, 0.7f },
		Vec3f{ 0.3f, 0.3f, 0.3f },
		MakeSphereCollider(),
		1));
	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.4f, 0.4f, 0.4f },
		Vec3f{ 0.6f, 0.6f, 0.6f },
		MakeSphereCollider(),
		2));

    const std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 3u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
	EXPECT_EQ(pairs[1], BodyPair(0, 2));
	EXPECT_EQ(pairs[2], BodyPair(1, 2));
}

TEST(GridHash, ClearRemovesExistingCellsAndPairs)
{
	GridHash hash{3};

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.9f, 0.9f, 0.9f },
		MakeSphereCollider(),
		0));
	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.2f, 0.2f, 0.2f },
		Vec3f{ 0.8f, 0.8f, 0.8f },
		MakeSphereCollider(),
		1));

    const std::span beforeClear(hash);
	ASSERT_EQ(beforeClear.size(), 1u);

	hash.Clear();

	const std::span afterClear(hash);
	EXPECT_TRUE(afterClear.empty());

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.2f, 0.2f, 0.2f },
		MakeSphereCollider(),
		0));
	ASSERT_TRUE(hash.Add(
		Vec3f{ 20.2f, 20.2f, 20.2f },
		Vec3f{ 20.1f, 20.1f, 20.1f },
		MakeSphereCollider(),
		1));

	const std::span nonOverlappingPairs(hash);
	EXPECT_TRUE(nonOverlappingPairs.empty());
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
		size_t BodyIndex;
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
		ASSERT_TRUE(hash2.Add(body.Min, body.Max, MakeSphereCollider(body.Radius), body.BodyIndex));
		ASSERT_TRUE(hash3.Add(body.Min, body.Max, MakeSphereCollider(body.Radius), body.BodyIndex));
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

	constexpr size_t kBodyCount = 1000;

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

        for(size_t i = 0; i < kBodyCount; ++i)
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

            ASSERT_TRUE(hash.Add(body.Min, body.Max, MakeSphereCollider(body.Radius), i));
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
