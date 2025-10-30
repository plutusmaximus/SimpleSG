#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

#include "STLLoader.h"

Result<void> loadAsciiSTL(const std::string& filename, std::vector<Triangle>& triangles)
{
    std::ifstream file(filename);
    expect(file.is_open(), "Could not open {}", filename);

    std::string line;
    Triangle tri{};
    Vec3f facetNormal{};
    bool readingFacet = false;
    int vertexCount = 0;

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string word;
        iss >> word;

        if (word == "facet")
        {
            std::string tmp;
            iss >> tmp; // should be "normal"
            iss >> facetNormal.x >> facetNormal.y >> facetNormal.z;
            facetNormal = facetNormal.Normalize();
            readingFacet = true;
            vertexCount = 0;
        }
        else if (word == "vertex" && readingFacet)
        {
            TVertex& v = tri.v[vertexCount];
            iss >> v.pos.x >> v.pos.y >> v.pos.z;
            v.normal = facetNormal;

            vertexCount++;
        }
        else if (word == "endfacet" && readingFacet)
        {
            triangles.push_back(tri);
            tri = {};
            readingFacet = false;
        }
    }

    file.close();

    expect(!triangles.empty(), "No triangles read from {}", filename);

    return {};
}
