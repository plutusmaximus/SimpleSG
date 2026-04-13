#pragma once

#include "Color.h"

class MaterialConstants
{
public:
    /// @brief Base color of the material.
    const RgbaColorf Color;

    /// @brief Metalness factor of the material.
    const float Metalness{ 0 };

    /// @brief Roughness factor of the material.
    const float Roughness{ 0 };

    // Align to 16 bytes for storage in a uniform/storage buffer.
    const float pad0;
    const float pad1;
};
