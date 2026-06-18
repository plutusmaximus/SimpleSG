#include "Bounds.h"

#include <gtest/gtest.h>

#include <vector>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
Vertex
MakeVertex(const Vec3f& p)
{
    Vertex v{};
    v.pos = p;
    v.normal = { 0.0f, 0.0f, 1.0f };
    v.uvs[0] = { .u = 0.0f, .v = 0.0f };
    return v;
}

void
ExpectVec3Eq(const Vec3f& actual, const Vec3f& expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
}
} // namespace

TEST(Box, Constructor_StoresProvidedMinAndMax)
{
    const Vec3f p0{ -3.0f, 2.0f, -7.5f };
    const Vec3f p1{ 4.0f, 9.0f, 1.25f };
    const Box box(p0, p1);

    ExpectVec3Eq(box.GetHalfExtents(), (p1 - p0) * 0.5f);
}

TEST(Box, FromVertices_EmptyInput_ReturnsZeroBox)
{
    const std::vector<Vertex> vertices;
    const std::vector<VertexIndex> indices;

    const Box box = Box::FromVertices(vertices, indices);

    ExpectVec3Eq(box.GetHalfExtents(), Vec3f{ 0 });
}

TEST(Box, FromVertices_SingleVertex_MinAndMaxMatchPoint)
{
    const std::vector<Vertex> vertices = { MakeVertex({ 2.5f, -4.0f, 8.0f }) };

    const std::vector<VertexIndex> indices = { 0 };

    const Box box = Box::FromVertices(vertices, indices);

    ExpectVec3Eq(box.GetHalfExtents(), Vec3f{ 0 });
}

TEST(Box, FromVertices_MultipleVertices_ComputesPerAxisExtrema)
{
    const std::vector<Vertex> vertices = {
        MakeVertex({ -1.0f, 4.0f, 0.5f }),
        MakeVertex({ 3.0f, -2.0f, 7.0f }),
        MakeVertex({ 0.25f, 9.0f, -5.0f }),
        MakeVertex({ -4.0f, 1.0f, 2.0f }),
    };

    const std::vector<VertexIndex> indices = { 0, 1, 2, 3 };

    const Box box = Box::FromVertices(vertices, indices);

    ExpectVec3Eq(box.GetHalfExtents(), Vec3f{ 3.5f, 5.5f, 6.0f });
}

TEST(Box, FromVertices_VertexOrderDoesNotAffectResult)
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

    const std::vector<VertexIndex> indices = { 0, 1, 2, 3 };

    const Box boxForward = Box::FromVertices(forward, indices);
    const Box boxReverse = Box::FromVertices(reverse, indices);

    ExpectVec3Eq(boxForward.GetHalfExtents(), boxReverse.GetHalfExtents());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)