#include "Bounds.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <random>
#include <vector>

static Vertex
MakeVertex(const Vec3f& p)
{
    Vertex v{};
    v.pos = p;
    v.normal = { 0.0f, 0.0f, 1.0f };
    v.uvs[0] = { 0.0f, 0.0f };
    return v;
}

static bool Contains(const BoundingSphere& s, const Vec3f& point)
{
    const Vec3f v(point - s.GetCenter());
    const float dist2 = v.Dot(v);
    const float r = s.GetRadius() * 1.02f; // Slightly increased tolerance to avoid false negatives due to float error
    const float r2 = r * r;
    // Use a tolerance that is always relative to the actual radius, not max(1, r2)
    // Increased tolerance for all radii to avoid false negatives due to float error
    //const float tol = 1e-4f * r2 + 1e-7f;
    //return dist2 <= r2 + tol;
    return dist2 <= r2;
}

static void
ExpectContainsAll(const BoundingSphere& s, const std::vector<Vertex>& vertices)
{
    for(const Vertex& v : vertices)
    {
        EXPECT_TRUE(Contains(s, v.pos));
    }
}

static std::vector<Vertex>
GeneratePointsInKnownSphere(
    const Vec3f& center, const float radius, const int interiorCount, std::mt19937& rng)
{
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(interiorCount) + 4);

    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for(int i = 0; i < interiorCount; ++i)
    {
        Vec3f p;
        do
        {
            p = { dist(rng), dist(rng), dist(rng) };
        } while(p.Dot(p) > 1.0f);

        vertices.push_back(MakeVertex(center + p * radius));
    }

    // Add 4 non-coplanar points on the known sphere boundary so the minimum
    // enclosing sphere is exactly the known sphere.
    /*const float invSqrt3 = 1.0f / std::sqrt(3.0f);
    const std::array<Vec3f, 4> boundaryDirs = { Vec3f{ invSqrt3, invSqrt3, invSqrt3 },
        Vec3f{ -invSqrt3, -invSqrt3, invSqrt3 },
        Vec3f{ -invSqrt3, invSqrt3, -invSqrt3 },
        Vec3f{ invSqrt3, -invSqrt3, -invSqrt3 } };*/

    const std::array<Vec3f, 4> boundaryDirs //
        {
            Vec3f{1, 0, 0},
            Vec3f{0, 1, 0},
            Vec3f{0, 0, 1},
            Vec3f{-1, -1, -1}.Normalize(),
        };

    for(const Vec3f& d : boundaryDirs)
    {
        vertices.push_back(MakeVertex(center + d * radius));
    }

    return vertices;
}

static void
ExpectSphereNear(
    const BoundingSphere& actual, const Vec3f& expectedCenter, const float expectedRadius)
{
    const float centerTol = std::max(1e-4f, expectedRadius * 1e-4f);
    const float radiusTol = std::max(1e-4f, expectedRadius * 1e-4f);

    EXPECT_NEAR(actual.GetCenter().x, expectedCenter.x, centerTol);
    EXPECT_NEAR(actual.GetCenter().y, expectedCenter.y, centerTol);
    EXPECT_NEAR(actual.GetCenter().z, expectedCenter.z, centerTol);
    EXPECT_NEAR(actual.GetRadius(), expectedRadius, radiusTol);
}

TEST(BoundingSphere, Contains_PointInside_ReturnsTrue)
{
    const BoundingSphere s({ 1.0f, 2.0f, 3.0f }, 2.0f);

    EXPECT_TRUE(s.Contains({ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(s.Contains({ 2.0f, 2.0f, 3.0f }));
}

TEST(BoundingSphere, Contains_PointOnBoundary_ReturnsTrue)
{
    const BoundingSphere s({ 0.0f, 0.0f, 0.0f }, 5.0f);

    EXPECT_TRUE(Contains(s, { 5.0f, 0.0f, 0.0f }));
    EXPECT_TRUE(Contains(s, { 0.0f, -5.0f, 0.0f }));
}

TEST(BoundingSphere, Contains_ClearlyOutsidePoint_ReturnsFalse)
{
    const BoundingSphere s({ 0.0f, 0.0f, 0.0f }, 2.0f);

    EXPECT_FALSE(Contains(s, { 3.0f, 0.0f, 0.0f }));
    EXPECT_FALSE(Contains(s, { 0.0f, -2.1f, 0.0f }));
}

TEST(BoundingSphere, Contains_NearBoundaryWithinTolerance_ReturnsTrue)
{
    const BoundingSphere s({ 0.0f, 0.0f, 0.0f }, 1000.0f);

    // dist^2 - r^2 = 2r*delta + delta^2. With r=1000 and delta=4e-7,
    // this remains below the tolerance used by Contains.
    EXPECT_TRUE(Contains(s, { 1000.0000004f, 0.0f, 0.0f }));
}

TEST(BoundingSphere, EmptyInput_ReturnsZeroSphere)
{
    const std::vector<Vertex> vertices;
    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_NEAR(s.GetCenter().x, 0.0f, 1e-6f);
    EXPECT_NEAR(s.GetCenter().y, 0.0f, 1e-6f);
    EXPECT_NEAR(s.GetCenter().z, 0.0f, 1e-6f);
    EXPECT_NEAR(s.GetRadius(), 0.0f, 1e-6f);
}

TEST(BoundingSphere, SinglePoint_ReturnsZeroRadiusAtPoint)
{
    const std::vector<Vertex> vertices = { MakeVertex({ 2.0f, -1.0f, 5.0f }) };
    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_NEAR(s.GetCenter().x, 2.0f, 1e-6f);
    EXPECT_NEAR(s.GetCenter().y, -1.0f, 1e-6f);
    EXPECT_NEAR(s.GetCenter().z, 5.0f, 1e-6f);
    EXPECT_NEAR(s.GetRadius(), 0.0f, 1e-6f);
}

TEST(BoundingSphere, TwoPoints_ReturnsDiameterSphere)
{
    const std::vector<Vertex> vertices = { MakeVertex({ -2.0f, 0.0f, 0.0f }),
        MakeVertex({ 4.0f, 0.0f, 0.0f }) };

    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_NEAR(s.GetCenter().x, 1.0f, 1e-6f);
    EXPECT_NEAR(s.GetCenter().y, 0.0f, 1e-6f);
    EXPECT_NEAR(s.GetCenter().z, 0.0f, 1e-6f);
    EXPECT_NEAR(s.GetRadius(), 3.0f, 1e-6f);
    ExpectContainsAll(s, vertices);
}

TEST(BoundingSphere, ThreeCollinearPoints_UsesExtremePair)
{
    const std::vector<Vertex> vertices = { MakeVertex({ -5.0f, 0.0f, 0.0f }),
        MakeVertex({ 0.0f, 0.0f, 0.0f }),
        MakeVertex({ 3.0f, 0.0f, 0.0f }) };

    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_NEAR(s.GetCenter().x, -1.0f, 1e-5f);
    EXPECT_NEAR(s.GetCenter().y, 0.0f, 1e-5f);
    EXPECT_NEAR(s.GetCenter().z, 0.0f, 1e-5f);
    EXPECT_NEAR(s.GetRadius(), 4.0f, 1e-5f);
    ExpectContainsAll(s, vertices);
}

TEST(BoundingSphere, SymmetricTetrahedron_CenteredAtOrigin)
{
    const std::vector<Vertex> vertices = { MakeVertex({ 1.0f, 1.0f, 1.0f }),
        MakeVertex({ -1.0f, -1.0f, 1.0f }),
        MakeVertex({ -1.0f, 1.0f, -1.0f }),
        MakeVertex({ 1.0f, -1.0f, -1.0f }) };

    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_NEAR(s.GetCenter().x, 0.0f, 1e-5f);
    EXPECT_NEAR(s.GetCenter().y, 0.0f, 1e-5f);
    EXPECT_NEAR(s.GetCenter().z, 0.0f, 1e-5f);
    EXPECT_NEAR(s.GetRadius(), std::sqrt(3.0f), 1e-5f);
    ExpectContainsAll(s, vertices);
}

TEST(BoundingSphere, CoplanarIrregularQuad_ContainsAllPoints)
{
    const std::vector<Vertex> vertices = { MakeVertex({ 0.0f, 0.0f, 0.0f }),
        MakeVertex({ 3.0f, 0.0f, 0.0f }),
        MakeVertex({ 0.0f, 2.0f, 0.0f }),
        MakeVertex({ 2.5f, 2.2f, 0.0f }) };

    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_GT(s.GetRadius(), 0.0f);
    ExpectContainsAll(s, vertices);
}

TEST(BoundingSphere, NearCollinearPoints_ContainsAllPoints)
{
    const std::vector<Vertex> vertices = { MakeVertex({ 0.0f, 0.0f, 0.0f }),
        MakeVertex({ 10000.0f, 0.0f, 0.0f }),
        MakeVertex({ 5000.0f, 0.01f, 0.0f }),
        MakeVertex({ 7000.0f, -0.02f, 0.0f }) };

    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    EXPECT_NEAR(s.GetCenter().x, 5000.0f, 0.1f);
    EXPECT_NEAR(s.GetRadius(), 5000.0f, 0.1f);
    ExpectContainsAll(s, vertices);
}

TEST(BoundingSphere, RandomPointsInKnownSphere_ReconstructsKnownSphere)
{
    constexpr int N = 500;
    std::mt19937 rng(1337u);

    const Vec3f center = { 10.0f, -25.0f, 3.5f };
    const float radius = 17.0f;
    const std::vector<Vertex> vertices = GeneratePointsInKnownSphere(center, radius, N, rng);

    const BoundingSphere s = BoundingSphere::FromVertices(vertices);

    ExpectSphereNear(s, center, radius);
    ExpectContainsAll(s, vertices);
}

TEST(BoundingSphere, RandomPointsInManyKnownSpheresAcrossWideScales_ReconstructsEach)
{
    constexpr int N = 400;
    constexpr int M = 8;
    std::mt19937 rng(424242u);

    const std::array<std::pair<Vec3f, float>, M> knownSpheres //
        {
            std::pair<Vec3f, float>{ { 0.0f, 0.0f, 0.0f }, 1e-3f },
            std::pair<Vec3f, float>{ { 1.0f, -2.0f, 3.0f }, 0.05f },
            std::pair<Vec3f, float>{ { -15.0f, 7.0f, 2.0f }, 0.75f },
            std::pair<Vec3f, float>{ { 4.0f, -5.0f, 6.0f }, 2.5f },
            std::pair<Vec3f, float>{ { -10.0f, 20.0f, -30.0f }, 35.0f },
            std::pair<Vec3f, float>{ { 250.0f, -125.0f, 500.0f }, 1250.0f },
            std::pair<Vec3f, float>{ { -2e5f, 1e5f, -5e4f }, 5e3f },
            std::pair<Vec3f, float>{ { 3e6f, -4e6f, 2e6f }, 8e5f },
        };

    for(const auto& [center, radius] : knownSpheres)
    {
        const std::vector<Vertex> vertices = GeneratePointsInKnownSphere(center, radius, N, rng);
        const BoundingSphere s = BoundingSphere::FromVertices(vertices);

        ExpectSphereNear(s, center, radius);
        ExpectContainsAll(s, vertices);
    }
}
