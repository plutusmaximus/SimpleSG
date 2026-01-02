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
    Result<const ModelSpec*> LoadFromFile(const std::string& key, const std::string& filePath);

    /// @brief Retrieves a previously loaded model.
    Result<const ModelSpec*> Get(const std::string& key) const;

    /// @brief Checks if a model with the given key exists in the catalog.
    bool Contains(const std::string& key) const { return m_Entries.find(key) != m_Entries.end(); }

    /// @brief Returns the number of models in the catalog.
    size_t Size() const { return m_Entries.size(); }

    /// @brief Clears all models from the catalog.
    void Clear() { m_Entries.clear(); }

private:

    std::unordered_map<std::string, ModelSpec> m_Entries;
};
