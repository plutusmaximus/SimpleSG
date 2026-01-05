#pragma once

#include "RefCount.h"
#include "Error.h"
#include "Image.h"
#include <string>
#include <algorithm>
#include <variant>

class GpuTexture;
class GpuVertexShader;
class GpuFragmentShader;

/// @brief RGBA color representation.
template<typename T>
struct RgbaColor
{
    RgbaColor()
        : RgbaColor(0, 0, 0, 0)
    {
    }

    RgbaColor(const T inR, const T inG, const T inB)
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    RgbaColor(const T inR, const T inG, const T inB, const T inA);

    T r, g, b, a;
};

using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

//Specialization for uint8_t to use 255 as max value.
inline RgbaColor<uint8_t>::RgbaColor(const uint8_t inR, const uint8_t inG, const uint8_t inB)
    : RgbaColor<uint8_t>(inR, inG, inB, 255)
{
}

//Specialization for float to assert values are in [0,1] range.
inline RgbaColor<float>::RgbaColor(const float inR, const float inG, const float inB, const float inA)
    : r(std::clamp(inR, 0.0f, 1.0f)), g(std::clamp(inG, 0.0f, 1.0f)), b(std::clamp(inB, 0.0f, 1.0f)), a(std::clamp(inA, 0.0f, 1.0f))
{
    eassert(inR >= 0 && inR <= 1);
    eassert(inG >= 0 && inG <= 1);
    eassert(inB >= 0 && inB <= 1);
    eassert(inA >= 0 && inA <= 1);
}

/// @brief Unique key for caching resources.
class CacheKey
{
    friend std::hash<CacheKey>;

public:
    explicit CacheKey(const std::string& key)
        : m_Key(key)
    {
        // CacheKey must not be empty.
        eassert(!key.empty());
    }

    bool operator==(const CacheKey& other) const
    {
        return m_Key == other.m_Key;
    }

    bool operator!=(const CacheKey& other) const
    {
        return m_Key != other.m_Key;
    }

    bool operator<(const CacheKey& other) const
    {
        return m_Key < other.m_Key;
    }

    const std::string& ToString() const
    {
        return m_Key;
    }

private:
    CacheKey() = delete;

    std::string m_Key;
};

/// @brief Specification for creating a texture.
class TextureSpec
{
public:

    /// @brief Constructs a texture spec from a file path.
    /// The cache key is set to the path.
    explicit TextureSpec(const std::string& path)
        : CacheKey(path)
        , Source(path)
    {
    }

    /// @brief Constructs a texture spec from an image.
    TextureSpec(const std::string& cacheKey, RefPtr<Image> image)
        : CacheKey(cacheKey)
        , Source(image)
    {
    }

    /// @brief Constructs a texture spec from a color.
    TextureSpec(const std::string& cacheKey, const RgbaColorf& color)
        : CacheKey(cacheKey)
        , Source(color)
    {
    }

    /// @brief Unique key for caching the texture.
    const CacheKey CacheKey;

    // FIXME(KB) - add support for resource paths.
    std::variant<std::string, RefPtr<Image>, RgbaColorf> Source;

private:
    TextureSpec() = delete;
};

/// @brief Unique identifier for a material.
class MaterialId
{
    static constexpr unsigned INVALID_VALUE = 0;

    friend std::hash<MaterialId>;

public:

    bool operator==(const MaterialId& other) const
    {
        return m_Value == other.m_Value;
    }

    bool operator!=(const MaterialId& other) const
    {
        return m_Value != other.m_Value;
    }

    bool operator<(const MaterialId& other) const
    {
        return m_Value < other.m_Value;
    }

    bool operator>(const MaterialId& other) const
    {
        return m_Value > other.m_Value;
    }

    bool operator<=(const MaterialId& other) const
    {
        return m_Value <= other.m_Value;
    }

    bool operator>=(const MaterialId& other) const
    {
        return m_Value >= other.m_Value;
    }

    static MaterialId NextId()
    {
        static unsigned NEXT = 0x01100011;

        unsigned next;
        for (next = NEXT++; INVALID_VALUE == next; next = NEXT++) {}

        return MaterialId{ next };
    }

private:

    const unsigned m_Value = INVALID_VALUE;

    explicit MaterialId(unsigned value) : m_Value(value) {}
};

/// @brief Flags defining material properties.
enum MaterialFlags : unsigned
{
    None = 0,
    Translucent = 1 << 0,
};

/// @brief Unique key identifying a material by its ID and flags.
/// Used to group meshes sharing the same material attributes.
class MaterialKey
{
public:
    MaterialKey(const MaterialId& id, const MaterialFlags flags)
        : Id(id)
        , Flags(flags)
    {
    }

    bool operator==(const MaterialKey& other) const
    {
        return (Id == other.Id) && (Flags == other.Flags);
    }

    bool operator!=(const MaterialKey& other) const
    {
        return !(*this == other);
    }

    bool operator<(const MaterialKey& other) const
    {
        if (Id != other.Id)
        {
            return Id < other.Id;
        }
        return Flags < other.Flags;
    }

    bool operator>(const MaterialKey& other) const
    {
        return other < *this;
    }

    bool operator<=(const MaterialKey& other) const
    {
        return !(other < *this);
    }

    bool operator>=(const MaterialKey& other) const
    {
        return !(*this < other);
    }

    const MaterialId Id;
    const MaterialFlags Flags{ MaterialFlags::None };
};

/// @brief Specification for creating a material.
struct MaterialSpec
{
    const RgbaColorf Color;

    const float Metalness{ 0 };
    const float Roughness{ 0 };

    const TextureSpec Albedo;

    const std::string VertexShaderPath;
    const std::string FragmentShaderPath;

    bool HasAlbedo() const
    {
        static_assert(std::variant_size_v<decltype(Albedo.Source)> == 3,
            "Unexpected number of variants in TextureSpec::Source");
        return std::get_if<std::string>(&Albedo.Source) != nullptr ||
               std::get_if<RefPtr<Image>>(&Albedo.Source) != nullptr;
    }
};

/// @brief Material used for rendering meshes.
class Material
{
public:

    Material(
        const RgbaColorf color,
        const float metalness,
        const float roughness,
        RefPtr<GpuTexture> albedo,
        RefPtr<GpuVertexShader> vertexShader,
        RefPtr<GpuFragmentShader> fragmentShader)
        : Key(MaterialId::NextId(), color.a < 1.0f ? MaterialFlags::Translucent : MaterialFlags::None)
        , Color(color)
        , Metalness(metalness)
        , Roughness(roughness)
        , Albedo(albedo)
        , VertexShader(vertexShader)
        , FragmentShader(fragmentShader)
    {
    }

    /// @brief Unique key identifying this material.
    /// Used to group geometry sharing the same material attributes.
    const MaterialKey Key;

    /// @brief Base color of the material.
    const RgbaColorf Color;

    /// @brief Metalness factor of the material.
    const float Metalness{ 0 };

    /// @brief Roughness factor of the material.
    const float Roughness{ 0 };

    /// @brief Albedo (base color) texture of the material.
    RefPtr<GpuTexture> const Albedo;

    /// @brief Vertex shader used by the material.
    const RefPtr<GpuVertexShader> VertexShader;

    /// @brief Fragment shader used by the material.
    const RefPtr<GpuFragmentShader> FragmentShader;

private:

    Material() = delete;
};

namespace std
{
    /// @brief Enable hashing of CacheKey for use in unordered containers.
    template<>
    struct hash<CacheKey>
    {
        std::size_t operator()(const CacheKey& key) const noexcept
        {
            return std::hash<std::string>()(key.m_Key);
        }
    };

    /// @brief Enable hashing of MaterialId for use in unordered containers.
    template<>
    struct hash<MaterialId>
    {
        std::size_t operator()(const MaterialId id) const noexcept
        {
            return static_cast<std::size_t>(id.m_Value);
        }
    };
}