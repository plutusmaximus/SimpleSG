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

    explicit ModelCatalog(RefPtr<GPUDevice> gpuDevice)
        : m_GpuDevice(gpuDevice)
    {
    }

    /// @brief Loads a model from file if not already loaded.
    Result<RefPtr<Model>> LoadModelFromFile(const std::string& key, const std::string& filePath);

    Result<RefPtr<Model>> CreateModel(const std::string& key, const ModelSpec& modelSpec);

    /// @brief Retrieves a previously loaded model.
    Result<RefPtr<Model>> GetModel(const std::string& key) const;

    /// @brief Checks if a model with the given key exists in the catalog.
    bool ContainsModel(const std::string& key) const { return m_Entries.find(key) != m_Entries.end(); }

    /// @brief Returns the number of models in the catalog.
    size_t Size() const { return m_Entries.size(); }

private:

    RefPtr<GPUDevice> m_GpuDevice;
    std::unordered_map<std::string, RefPtr<Model>> m_Entries;
};
