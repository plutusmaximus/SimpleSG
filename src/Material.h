#pragma once

#include "CacheKey.h"
#include "Color.h"
#include <variant>

class GpuTexture;

/// @brief Specification for creating a texture.
class TextureSpec
{
public:

    /// @brief Represents no texture.
    struct None_t{};

    explicit TextureSpec(None_t)
        : Source(None_t{})
    {
    }

    /// @brief Constructs a texture spec from a file path.
    /// The cache key is set to the path.
    explicit TextureSpec(const imstring& path)
        : Source(path)
        , m_CacheKey(CacheKey(path))
    {
    }

    /// @brief Constructs a texture spec from a color.
    TextureSpec(const RgbaColorf& color)
        : Source(color)
        , m_CacheKey(CacheKey(color.ToHexString()))
    {
    }

    /// @brief Returns true if the texture spec is valid (i.e., has a specified source).
    bool IsValid() const
    {
        return !std::holds_alternative<None_t>(Source);
    }

    bool TryGetPath(imstring& outPath) const
    {
        if (const auto* path = std::get_if<imstring>(&Source))
        {
            outPath = *path;
            return true;
        }
        return false;
    }

    bool TryGetColor(RgbaColorf& outColor) const
    {
        if (const auto* color = std::get_if<const RgbaColorf>(&Source))
        {
            outColor = *color;
            return true;
        }
        return false;
    }

    CacheKey GetCacheKey() const
    {
        if(!everify(!std::holds_alternative<None_t>(Source), "TextureSpec has no source"))
        {
            return CacheKey("");
        }

        if(std::holds_alternative<imstring>(Source))
        {
            return m_CacheKey.value();
        }

        if(std::holds_alternative<const RgbaColorf>(Source))
        {
            return m_CacheKey.value();
        }

        eassert(false, "Unhandled TextureSpec source type");
        return CacheKey("");
    }

    std::variant<None_t, imstring, const RgbaColorf> Source;

private:
    TextureSpec() = delete;

    std::optional<CacheKey> m_CacheKey;
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

struct MaterialConstants
{
    const RgbaColorf Color;

    const float Metalness{ 0 };
    const float Roughness{ 0 };
};

/// @brief Specification for creating a material.
struct MaterialSpec
{
    const MaterialConstants Constants;

    const TextureSpec BaseTexture;
};

/// @brief Material used for rendering meshes.
class Material
{
public:

    Material(
        const RgbaColorf color,
        const float metalness,
        const float roughness,
        GpuTexture* baseTexture)
        : m_Key(MaterialId::NextId(), color.a < 1.0f ? MaterialFlags::Translucent : MaterialFlags::None)
        , m_Color(color)
        , m_Metalness(metalness)
        , m_Roughness(roughness)
        , m_BaseTexture(baseTexture)
    {
    }

    Material(const Material&) = default;
    Material& operator=(const Material&) = default;
    Material(Material&&) = default;
    Material& operator=(Material&&) = default;

    const MaterialId GetId() const { return m_Key.Id; }
    const MaterialKey& GetKey() const { return m_Key; }
    const RgbaColorf& GetColor() const { return m_Color; }
    float GetMetalness() const { return m_Metalness; }
    float GetRoughness() const { return m_Roughness; }
    GpuTexture* GetBaseTexture() const { return m_BaseTexture; }

private:

    Material() = delete;

    /// @brief Unique key identifying this material.
    /// Used to group geometry sharing the same material attributes.
    MaterialKey m_Key;

    /// @brief Base color of the material.
    RgbaColorf m_Color;

    /// @brief Metalness factor of the material.
    float m_Metalness{ 0 };

    /// @brief Roughness factor of the material.
    float m_Roughness{ 0 };

    /// @brief Base (albedo) texture of the material.
    GpuTexture* m_BaseTexture{ nullptr };
};

/// @brief Enable hashing of MaterialId for use in unordered containers.
template<>
struct std::hash<MaterialId>
{
    std::size_t operator()(const MaterialId id) const noexcept
    {
        return static_cast<std::size_t>(id.m_Value);
    }
};
