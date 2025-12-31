#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <expected>

#include "Error.h"
#include "Vertex.h"
#include "Mesh.h"
#include "Model.h"

/// @brief A catalog for loading and storing 3D model specifications.
class ModelCatalog
{
public:
    /// @brief Loads a model from file if not already loaded.
    Result<ModelSpec> LoadFromFile(const std::string& key, const std::string& filePath);

    /// @brief Retrieves a previously loaded model.
    Result<ModelSpec> Get(const std::string& key) const;

    /// @brief Checks if a model with the given key exists in the catalog.
    bool Contains(const std::string& key) const { return m_Entries.find(key) != m_Entries.end(); }

    /// @brief Returns the number of models in the catalog.
    size_t Size() const { return m_Entries.size(); }

    /// @brief Clears all models from the catalog.
    void Clear() { m_Entries.clear(); }

private:
    struct Entry
    {
        std::vector<Vertex> Vertices;
        std::vector<VertexIndex> Indices;
        std::vector<MeshSpec> MeshSpecs;

        ModelSpec ToSpec() const
        {
            return ModelSpec
            {
                .Vertices = std::span<const Vertex>(Vertices.data(), Vertices.size()),
                .Indices = std::span<const VertexIndex>(Indices.data(), Indices.size()),
                .MeshSpecs = std::span<const MeshSpec>(MeshSpecs.data(), MeshSpecs.size()),
            };
        }
    };

    std::unordered_map<std::string, Entry> m_Entries;
};
