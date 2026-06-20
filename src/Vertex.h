#pragma once

#include "VecMath.h"

#include <climits>

struct UV2
{
    float u, v;
};

using VertexPos = Vec3f;
using VertexNormal = Vec3f;

template<size_t NUM_UVS>
struct VertexT
{
    static constexpr size_t kNumUVs = NUM_UVS;

    VertexPos pos;
    VertexNormal normal;
    UV2 uvs[NUM_UVS];
};

template<>
struct VertexT<0>
{
    static constexpr size_t kNumUVs = 0;

    VertexPos pos;
    VertexNormal normal;
};

using Vertex = VertexT<1>;

using VertexIndex = uint32_t;

constexpr int VERTEX_INDEX_BITS = sizeof(VertexIndex) * CHAR_BIT;