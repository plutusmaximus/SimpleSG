#include <gtest/gtest.h>

#include "PhysicsSolver.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace
{
	using TestGridHash5 = GridHash<5>;
    using TestGridHash4 = GridHash<4>;

	Collider MakeSphereCollider(float radius)
	{
		return Collider{ SphereCollider{ radius } };
	}

	Collider MakeSphereCollider()
	{
		return Collider{ SphereCollider{ 0.1f } };
	}
}

TEST(GridHash, EmptyGridHasNoPotentialCollisions)
{
	TestGridHash5 hash;

    std::span pairs(hash);

	EXPECT_TRUE(pairs.empty());
}

TEST(GridHash, SingleBodyProducesNoPairs)
{
	TestGridHash5 hash;

	const Result<> result = hash.Add(
		Vec3f{ 0.2f, 0.2f, 0.2f },
		Vec3f{ 0.8f, 0.8f, 0.8f },
		MakeSphereCollider(),
		3);

	ASSERT_TRUE(result);

    std::span pairs(hash);
	EXPECT_TRUE(pairs.empty());
}

TEST(GridHash, TwoBodiesInSameCellProduceOneOrderedPair)
{
	TestGridHash5 hash;

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.9f, 0.9f, 0.9f },
		MakeSphereCollider(),
		7));

	ASSERT_TRUE(hash.Add(
		Vec3f{ 1.0f, 1.0f, 1.0f },
		Vec3f{ 1.8f, 1.8f, 1.8f },
		MakeSphereCollider(),
		3));

    std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(3, 7));
}

TEST(GridHash, SharedAcrossManyCellsStillProducesUniquePair)
{
	TestGridHash5 hash;

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

    std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 1u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
}

TEST(GridHash, ThreeBodiesInOneCellGenerateAllUniquePairs)
{
	TestGridHash5 hash;

	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.1f, 0.1f, 0.1f },
		Vec3f{ 0.9f, 0.9f, 0.9f },
		MakeSphereCollider(),
		0));
	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.3f, 0.3f, 0.3f },
		Vec3f{ 0.7f, 0.7f, 0.7f },
		MakeSphereCollider(),
		1));
	ASSERT_TRUE(hash.Add(
		Vec3f{ 0.4f, 0.4f, 0.4f },
		Vec3f{ 0.6f, 0.6f, 0.6f },
		MakeSphereCollider(),
		2));

    std::span pairs(hash);

	ASSERT_EQ(pairs.size(), 3u);
	EXPECT_EQ(pairs[0], BodyPair(0, 1));
	EXPECT_EQ(pairs[1], BodyPair(0, 2));
	EXPECT_EQ(pairs[2], BodyPair(1, 2));
}

TEST(GridHash, ClearRemovesExistingCellsAndPairs)
{
	TestGridHash5 hash;

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

    std::span beforeClear(hash);
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
		Vec3f{ 20.1f, 20.1f, 20.1f },
		Vec3f{ 20.2f, 20.2f, 20.2f },
		MakeSphereCollider(),
		1));

	const std::span nonOverlappingPairs(hash);
	EXPECT_TRUE(nonOverlappingPairs.empty());
}

TEST(GridHash, GridHash4AndGridHash5ContainSameBodyPairSet)
{
	TestGridHash4 hash4;
	TestGridHash5 hash5;

	struct BodyInput
	{
		Vec3f Min;
		Vec3f Max;
		float Radius;
		size_t BodyIndex;
	};

	const std::vector<BodyInput> bodies = {
		{ Vec3f{ 0.0f, 0.0f, 0.0f }, Vec3f{ 2.0f, 2.0f, 2.0f }, 0.25f, 0 },
		{ Vec3f{ 1.0f, 1.0f, 1.0f }, Vec3f{ 3.0f, 3.0f, 3.0f }, 0.25f, 1 },
		{ Vec3f{ 10.0f, 0.0f, 0.0f }, Vec3f{ 12.0f, 2.0f, 2.0f }, 0.25f, 2 },
		{ Vec3f{ 11.0f, 1.0f, 1.0f }, Vec3f{ 13.0f, 3.0f, 3.0f }, 0.25f, 3 },
		{ Vec3f{ 50.0f, 50.0f, 50.0f }, Vec3f{ 52.0f, 52.0f, 52.0f }, 0.25f, 4 },
	};

	for(const BodyInput& body : bodies)
	{
		ASSERT_TRUE(hash4.Add(body.Min, body.Max, MakeSphereCollider(body.Radius), body.BodyIndex));
		ASSERT_TRUE(hash5.Add(body.Min, body.Max, MakeSphereCollider(body.Radius), body.BodyIndex));
	}

	std::vector<BodyPair> pairs4(hash4.begin(), hash4.end());
	std::vector<BodyPair> pairs5(hash5.begin(), hash5.end());

	std::sort(pairs4.begin(), pairs4.end());
	std::sort(pairs5.begin(), pairs5.end());

	EXPECT_EQ(pairs4, pairs5);
}

TEST(GridHash, ChaosRandomizedBodies_AllExpectedPairsExist)
{
	TestGridHash5 hash;

	struct RandomBody
	{
		Vec3f Min;
		Vec3f Max;
		float Radius;
		std::array<int64_t, 3> MinQ;
		std::array<int64_t, 3> MaxQ;
	};

	constexpr size_t kBodyCount = 1000;
	constexpr float kCellSize = static_cast<float>(TestGridHash5::kCellSize);

	const auto quantize = [](float value)
	{
		return static_cast<int64_t>(std::floor(value / kCellSize));
	};

	std::mt19937 rng(0xC0FFEEu);
	std::uniform_real_distribution<float> centerDist(-400.0f, 400.0f);
	std::uniform_real_distribution<float> halfExtentDist(0.05f, 3.0f);
	std::uniform_real_distribution<float> radiusDist(0.01f, 1.25f);

	std::vector<RandomBody> bodies;
	bodies.reserve(kBodyCount);

	for(size_t i = 0; i < kBodyCount; ++i)
    {
        const Vec3f center //
            {
                centerDist(rng),
                centerDist(rng),
                centerDist(rng),
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

        RandomBody body //
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

    std::sort(expectedPairs.begin(), expectedPairs.end());
	std::sort(actualPairs.begin(), actualPairs.end());

    for(size_t i = 0; i < expectedPairs.size(); ++i)
    {
        EXPECT_EQ(expectedPairs[i], actualPairs[i]);
    }
}
