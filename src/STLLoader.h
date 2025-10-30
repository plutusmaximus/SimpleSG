#pragma once

#include <string>
#include <vector>
#include <expected>

#include "Vertex.h"

#include "Error.h"

struct TVertex : public Vertex
{
    //Used for sorting/uniquifying/merging vertices
    bool operator<(const TVertex& other) const
    {
        //return std::memcmp(this, &other, sizeof(*this)) < 0;
        return std::memcmp(&pos, &other.pos, sizeof(pos)) < 0;
    }
};

struct Triangle
{
    TVertex v[3];
};

Result<void> loadAsciiSTL(const std::string& filename, std::vector<Triangle>& triangles);
