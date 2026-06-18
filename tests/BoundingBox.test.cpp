#include "Bounds.h"

#include <gtest/gtest.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
void
ExpectVec3Eq(const Vec3f& actual, const Vec3f& expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
}
} // namespace

TEST(BoundingBox, Constructor_ComputesCenterAndHalfExtents)
{
    const BoundingBox box(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(4.0f, 2.0f, 6.0f));

    ExpectVec3Eq(box.GetCenter(), Vec3f(2.0f, 1.0f, 3.0f));
    ExpectVec3Eq(box.GetHalfExtents(), Vec3f(2.0f, 1.0f, 3.0f));
}

TEST(BoundingBox, Constructor_WithUnorderedPointsStillProducesPositiveHalfExtents)
{
    const BoundingBox box(Vec3f(4.0f, 2.0f, 6.0f), Vec3f(0.0f, 0.0f, 0.0f));

    ExpectVec3Eq(box.GetCenter(), Vec3f(2.0f, 1.0f, 3.0f));
    ExpectVec3Eq(box.GetHalfExtents(), Vec3f(2.0f, 1.0f, 3.0f));
}

TEST(BoundingBox, MergeOperator_EnclosesBothBoxes)
{
    const BoundingBox a(Vec3f(-2.0f, -1.0f, -3.0f), Vec3f(2.0f, 1.0f, 3.0f));
    const BoundingBox b(Vec3f(1.0f, -5.0f, -1.0f), Vec3f(8.0f, 4.0f, 10.0f));

    const BoundingBox merged = a + b;

    ExpectVec3Eq(merged.GetCenter(), Vec3f(3.0f, -0.5f, 3.5f));
    ExpectVec3Eq(merged.GetHalfExtents(), Vec3f(5.0f, 4.5f, 6.5f));
}

TEST(BoundingBox, TranslateOperator_ReturnsOffsetBox)
{
    const BoundingBox box(Vec3f(-1.0f, -2.0f, -3.0f), Vec3f(1.0f, 2.0f, 3.0f));
    const Vec3f offset(3.0f, -5.0f, 7.0f);

    const BoundingBox translated = box + offset;

    ExpectVec3Eq(translated.GetCenter(), Vec3f(3.0f, -5.0f, 7.0f));
    ExpectVec3Eq(translated.GetHalfExtents(), box.GetHalfExtents());
}

TEST(BoundingBox, CompoundMerge_AssignsMergedBounds)
{
    BoundingBox a(Vec3f(-2.0f, -2.0f, -2.0f), Vec3f(0.0f, 0.0f, 0.0f));
    const BoundingBox b(Vec3f(-1.0f, -1.0f, -1.0f), Vec3f(3.0f, 5.0f, 7.0f));

    a += b;

    ExpectVec3Eq(a.GetCenter(), Vec3f(0.5f, 1.5f, 2.5f));
    ExpectVec3Eq(a.GetHalfExtents(), Vec3f(2.5f, 3.5f, 4.5f));
}

TEST(BoundingBox, CompoundTranslate_AssignsOffsetBounds)
{
    BoundingBox box(Vec3f(-2.0f, -4.0f, -6.0f), Vec3f(2.0f, 4.0f, 6.0f));

    box += Vec3f(1.0f, 2.0f, 3.0f);

    ExpectVec3Eq(box.GetCenter(), Vec3f(1.0f, 2.0f, 3.0f));
    ExpectVec3Eq(box.GetHalfExtents(), Vec3f(2.0f, 4.0f, 6.0f));
}

TEST(BoundingBox, FromVertices_UsesProvidedIndices)
{
    const Vertex vertices[] //
        {
            { .pos = Vec3f(-1.0f, 2.0f, 3.0f),
                .normal = Vec3f(0.0f, 1.0f, 0.0f),
                .uvs = { { .u = 0.0f, .v = 0.0f } } },
            { .pos = Vec3f(4.0f, -2.0f, 5.0f),
                .normal = Vec3f(0.0f, 1.0f, 0.0f),
                .uvs = { { .u = 1.0f, .v = 0.0f } } },
            { .pos = Vec3f(10.0f, 10.0f, 10.0f),
                .normal = Vec3f(0.0f, 1.0f, 0.0f),
                .uvs = { { .u = 1.0f, .v = 1.0f } } },
            { .pos = Vec3f(0.0f, 8.0f, -1.0f),
                .normal = Vec3f(0.0f, 1.0f, 0.0f),
                .uvs = { { .u = 0.0f, .v = 1.0f } } },
        };

    const VertexIndex indices[] = { 0, 1, 3 };

    const BoundingBox box = BoundingBox::FromVertices(vertices, indices);

    ExpectVec3Eq(box.GetCenter(), Vec3f(1.5f, 3.0f, 2.0f));
    ExpectVec3Eq(box.GetHalfExtents(), Vec3f(2.5f, 5.0f, 3.0f));
}

TEST(BoundingBox, FromVertices_SingleIndexProducesDegenerateBox)
{
    const Vertex vertices[] //
        {
            { .pos = Vec3f(-3.5f, 1.25f, 9.0f),
                .normal = Vec3f(0.0f, 1.0f, 0.0f),
                .uvs = { { .u = 0.0f, .v = 0.0f } } },
            { .pos = Vec3f(0.0f, 0.0f, 0.0f),
                .normal = Vec3f(0.0f, 1.0f, 0.0f),
                .uvs = { { .u = 1.0f, .v = 1.0f } } },
        };

    const VertexIndex indices[] = { 0 };

    const BoundingBox box = BoundingBox::FromVertices(vertices, indices);

    ExpectVec3Eq(box.GetCenter(), Vec3f(-3.5f, 1.25f, 9.0f));
    ExpectVec3Eq(box.GetHalfExtents(), Vec3f(0.0f, 0.0f, 0.0f));
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
