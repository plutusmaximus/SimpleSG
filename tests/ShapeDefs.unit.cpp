#include "ShapeDefs.h"

#include <gtest/gtest.h>
#include <SDL3/SDL_assert.h>

#include <cmath>
#include <initializer_list>

namespace
{
/// Lets invalid-input tests inspect the failed Result after MLG_CHECKV reports an assertion.
class IgnoreShapeAssertions final
{
public:
    IgnoreShapeAssertions()
        : m_PreviousHandler(SDL_GetAssertionHandler(&m_PreviousUserData))
    {
        SDL_SetAssertionHandler(&Ignore, nullptr);
    }

    ~IgnoreShapeAssertions() { SDL_SetAssertionHandler(m_PreviousHandler, m_PreviousUserData); }

    IgnoreShapeAssertions(const IgnoreShapeAssertions&) = delete;
    IgnoreShapeAssertions& operator=(const IgnoreShapeAssertions&) = delete;
    IgnoreShapeAssertions(IgnoreShapeAssertions&&) = delete;
    IgnoreShapeAssertions& operator=(IgnoreShapeAssertions&&) = delete;

private:
    static SDL_AssertState SDLCALL Ignore(const SDL_AssertData*, void*)
    {
        return SDL_ASSERTION_IGNORE;
    }

    void* m_PreviousUserData{ nullptr };
    SDL_AssertionHandler m_PreviousHandler{ nullptr };
};

void
ExpectUsableTriangles(const MeshDef& mesh)
{
    ASSERT_EQ(mesh.Indices.size() % 3, 0uz);
    for (size_t i = 0; i < mesh.Indices.size(); i += 3)
    {
        ASSERT_LT(mesh.Indices[i], mesh.Vertices.size());
        ASSERT_LT(mesh.Indices[i + 1], mesh.Vertices.size());
        ASSERT_LT(mesh.Indices[i + 2], mesh.Vertices.size());
        const Vertex& a = mesh.Vertices[mesh.Indices[i]];
        const Vertex& b = mesh.Vertices[mesh.Indices[i + 1]];
        const Vertex& c = mesh.Vertices[mesh.Indices[i + 2]];

        const float abx = b.pos.x - a.pos.x;
        const float aby = b.pos.y - a.pos.y;
        const float abz = b.pos.z - a.pos.z;
        const float acx = c.pos.x - a.pos.x;
        const float acy = c.pos.y - a.pos.y;
        const float acz = c.pos.z - a.pos.z;
        const float cx = (aby * acz) - (abz * acy);
        const float cy = (abz * acx) - (abx * acz);
        const float cz = (abx * acy) - (aby * acx);
        EXPECT_GT((cx * cx) + (cy * cy) + (cz * cz), 1e-10f)
            << "triangle " << (i / 3);

        const float uvArea =
            ((b.uvs[0].u - a.uvs[0].u) * (c.uvs[0].v - a.uvs[0].v)) -
            ((b.uvs[0].v - a.uvs[0].v) * (c.uvs[0].u - a.uvs[0].u));
        EXPECT_GT(std::abs(uvArea), 1e-8f) << "triangle " << (i / 3);
    }

    for (const Vertex& vertex : mesh.Vertices)
    {
        EXPECT_GE(vertex.uvs[0].u, 0.0f);
        EXPECT_LE(vertex.uvs[0].u, 1.0f);
        EXPECT_GE(vertex.uvs[0].v, 0.0f);
        EXPECT_LE(vertex.uvs[0].v, 1.0f);
    }
}
}

TEST(ShapeDefs, BoxHasFlatFacesAndPlanarUvs)
{
    const MeshDef box = ShapeDefs::Box({ .Width = 2.0f, .Height = 3.0f, .Depth = 4.0f });
    ASSERT_EQ(box.Vertices.size(), 24uz);
    ASSERT_EQ(box.Indices.size(), 36uz);
    constexpr size_t kNumFaces = 6;
    for (size_t face = 0; face < kNumFaces; ++face)
    {
        const Vertex& first = box.Vertices[face * 4];
        for (size_t corner = 1; corner < 4; ++corner)
        {
            const Vertex& vertex = box.Vertices[(face * 4) + corner];
            EXPECT_FLOAT_EQ(vertex.normal.x, first.normal.x);
            EXPECT_FLOAT_EQ(vertex.normal.y, first.normal.y);
            EXPECT_FLOAT_EQ(vertex.normal.z, first.normal.z);
        }
        EXPECT_FLOAT_EQ(first.uvs[0].u, 0.0f);
        EXPECT_FLOAT_EQ(first.uvs[0].v, 0.0f);
        EXPECT_FLOAT_EQ(box.Vertices[(face * 4) + 2].uvs[0].u, 1.0f);
        EXPECT_FLOAT_EQ(box.Vertices[(face * 4) + 2].uvs[0].v, 1.0f);
    }
    ExpectUsableTriangles(box);
}

TEST(ShapeDefs, BallUsesFaceAtlasWithoutCollapsedUvs)
{
    const MeshDef ball = ShapeDefs::Ball({ .Radius = 1.0f, .Smoothness = 10.0f });
    EXPECT_EQ(ball.Vertices.size(), 900uz);
    EXPECT_EQ(ball.Indices.size(), 3840uz);
    ExpectUsableTriangles(ball);
}

TEST(ShapeDefs, CylinderHasSideSeamAndPlanarCaps)
{
    const MeshDef cylinder = ShapeDefs::Cylinder(
        { .Height = 2.0f, .Radius = 1.0f, .Smoothness = 1.0f });
    ASSERT_EQ(cylinder.Vertices.size(), 52uz);
    EXPECT_EQ(cylinder.Indices.size(), 144uz);
    EXPECT_FLOAT_EQ(cylinder.Vertices[0].pos.x, cylinder.Vertices[24].pos.x);
    EXPECT_FLOAT_EQ(cylinder.Vertices[0].pos.z, cylinder.Vertices[24].pos.z);
    EXPECT_FLOAT_EQ(cylinder.Vertices[0].uvs[0].u, 0.0f);
    EXPECT_FLOAT_EQ(cylinder.Vertices[24].uvs[0].u, 1.0f);
    EXPECT_FLOAT_EQ(cylinder.Vertices[25].uvs[0].v, 1.0f);
    ExpectUsableTriangles(cylinder);
}

TEST(ShapeDefs, ConeTipsHaveNoCollapsedSideTriangles)
{
    for (const auto& radii : {
             ShapeDefs::ConeParams{ .Radius1 = 0.0f, .Radius2 = 1.0f, .Smoothness = 1.0f },
             ShapeDefs::ConeParams{ .Radius1 = 1.0f, .Radius2 = 0.0f, .Smoothness = 1.0f },
             ShapeDefs::ConeParams{ .Radius1 = 1.0f, .Radius2 = 2.0f, .Smoothness = 1.0f } })
    {
        const MeshDef cone = ShapeDefs::Cone(radii);
        EXPECT_NEAR(cone.Vertices[0].normal.y,
            (radii.Radius1 - radii.Radius2) / std::sqrt(1.0f +
                ((radii.Radius2 - radii.Radius1) * (radii.Radius2 - radii.Radius1))), 1e-6f);
        ExpectUsableTriangles(cone);
    }
}

TEST(ShapeDefs, TorusHasBothTextureSeams)
{
    const MeshDef torus = ShapeDefs::Torus(
        { .RingRadius = 2.0f, .TubeRadius = 1.0f, .Smoothness = 1.0f });
    ASSERT_EQ(torus.Vertices.size(), 289uz);
    EXPECT_EQ(torus.Indices.size(), 1536uz);
    EXPECT_FLOAT_EQ(torus.Vertices[0].pos.x, torus.Vertices[16].pos.x);
    EXPECT_FLOAT_EQ(torus.Vertices[0].pos.x, torus.Vertices[272].pos.x);
    EXPECT_FLOAT_EQ(torus.Vertices[16].uvs[0].v, 1.0f);
    EXPECT_FLOAT_EQ(torus.Vertices[272].uvs[0].u, 1.0f);
    ExpectUsableTriangles(torus);
}
