#pragma once

#include "VecMath.h"

struct UV2
{
    float u, v;
};

using VertexPos = Vec3f;
using VertexNormal = Vec3f;

template<int NUM_UVS>
struct VertexT
{
    VertexPos pos;
    VertexNormal normal;
    UV2 uvs[NUM_UVS];
};

template<>
struct VertexT<0>
{
    VertexPos pos;
    VertexNormal normal;
};

using Vertex = VertexT<1>;

using VertexIndex = uint32_t;

constexpr int VERTEX_INDEX_BITS = sizeof(VertexIndex) * CHAR_BIT;