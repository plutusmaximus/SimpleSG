#include "Bounds.h"

#include <gtest/gtest.h>

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

static void
ExpectVec3Eq(const Vec3f& actual, const Vec3f& expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
}

TEST(AABB, DefaultConstructor_InitializesToZero)
{
    const AABB box;

    ExpectVec3Eq(box.GetMin(), { 0.0f, 0.0f, 0.0f });
    ExpectVec3Eq(box.GetMax(), { 0.0f, 0.0f, 0.0f });
}

TEST(AABB, Constructor_StoresProvidedMinAndMax)
{
    const AABB box({ -3.0f, 2.0f, -7.5f }, { 4.0f, 9.0f, 1.25f });

    ExpectVec3Eq(box.GetMin(), { -3.0f, 2.0f, -7.5f });
    ExpectVec3Eq(box.GetMax(), { 4.0f, 9.0f, 1.25f });
}

TEST(AABB, FromVertices_EmptyInput_ReturnsZeroBox)
{
    const std::vector<Vertex> vertices;

    const AABB box = AABB::FromVertices(vertices);

    ExpectVec3Eq(box.GetMin(), { 0.0f, 0.0f, 0.0f });
    ExpectVec3Eq(box.GetMax(), { 0.0f, 0.0f, 0.0f });
}

TEST(AABB, FromVertices_SingleVertex_MinAndMaxMatchPoint)
{
    const std::vector<Vertex> vertices = { MakeVertex({ 2.5f, -4.0f, 8.0f }) };

    const AABB box = AABB::FromVertices(vertices);

    ExpectVec3Eq(box.GetMin(), { 2.5f, -4.0f, 8.0f });
    ExpectVec3Eq(box.GetMax(), { 2.5f, -4.0f, 8.0f });
}

TEST(AABB, FromVertices_MultipleVertices_ComputesPerAxisExtrema)
{
    const std::vector<Vertex> vertices = {
        MakeVertex({ -1.0f, 4.0f, 0.5f }),
        MakeVertex({ 3.0f, -2.0f, 7.0f }),
        MakeVertex({ 0.25f, 9.0f, -5.0f }),
        MakeVertex({ -4.0f, 1.0f, 2.0f }),
    };

    const AABB box = AABB::FromVertices(vertices);

    ExpectVec3Eq(box.GetMin(), { -4.0f, -2.0f, -5.0f });
    ExpectVec3Eq(box.GetMax(), { 3.0f, 9.0f, 7.0f });
}

TEST(AABB, FromVertices_VertexOrderDoesNotAffectResult)
{
    const std::vector<Vertex> forward = {
        MakeVertex({ 5.0f, 1.0f, -3.0f }),
        MakeVertex({ -2.0f, 4.0f, 9.0f }),
        MakeVertex({ 7.5f, -6.0f, 2.0f }),
        MakeVertex({ 0.0f, 8.0f, -1.0f }),
    };

    const std::vector<Vertex> reverse = {
        forward[3],
        forward[2],
        forward[1],
        forward[0],
    };

    const AABB boxForward = AABB::FromVertices(forward);
    const AABB boxReverse = AABB::FromVertices(reverse);

    ExpectVec3Eq(boxForward.GetMin(), boxReverse.GetMin());
    ExpectVec3Eq(boxForward.GetMax(), boxReverse.GetMax());
}