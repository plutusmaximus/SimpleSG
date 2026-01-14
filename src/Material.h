#pragma once

#include "Error.h"
#include "Image.h"
#include <string>
#include <algorithm>
#include <variant>

class GpuVertexShader;
class GpuFragmentShader;

/// @brief RGBA color representation.
template<typename T>
class RgbaColor
{
public:
    constexpr RgbaColor()
        : RgbaColor(0, 0, 0, 0)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB)
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB, const T inA)
        : r(inR), g(inG), b(inB), a(inA)
    {
    }

    /// @brief  Conversion constructor.
    template<typename U>
    constexpr RgbaColor(const RgbaColor<U>& other);

    /// @brief Converts the color to a hexadecimal string representation - #RRGGBBAA
    std::string ToHexString() const;

    bool operator==(const RgbaColor& other) const
    {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const RgbaColor& other) const
    {
        return !(*this == other);
    }

    T r, g, b, a;
};

using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

/// @brief Specialization for uint8_t with default alpha of 255.
inline constexpr RgbaColor<uint8_t>::RgbaColor(const uint8_t inR, const uint8_t inG, const uint8_t inB)
    : RgbaColor<uint8_t>(inR, inG, inB, 255)
{
}

/// @brief Specialization for converting from float to uint8_t.
template<>
template<>
inline RgbaColor<uint8_t>::RgbaColor(const RgbaColor<float>& other)
    : r(static_cast<uint8_t>(std::clamp(other.r * 255.0f, 0.0f, 255.0f)))
    , g(static_cast<uint8_t>(std::clamp(other.g * 255.0f, 0.0f, 255.0f)))
    , b(static_cast<uint8_t>(std::clamp(other.b * 255.0f, 0.0f, 255.0f)))
    , a(static_cast<uint8_t>(std::clamp(other.a * 255.0f, 0.0f, 255.0f)))
{
}

/// @brief Specialization for float with clamping between 0.0 and 1.0.
inline constexpr RgbaColor<float>::RgbaColor(const float inR, const float inG, const float inB, const float inA)
    : r(std::clamp(inR, 0.0f, 1.0f)), g(std::clamp(inG, 0.0f, 1.0f)), b(std::clamp(inB, 0.0f, 1.0f)), a(std::clamp(inA, 0.0f, 1.0f))
{
    eassert(inR >= 0 && inR <= 1);
    eassert(inG >= 0 && inG <= 1);
    eassert(inB >= 0 && inB <= 1);
    eassert(inA >= 0 && inA <= 1);
}

/// @brief Specialization for converting from uint8_t to float.
template<>
template<>
inline RgbaColor<float>::RgbaColor(const RgbaColor<uint8_t>& other)
    : r(static_cast<float>(other.r) / 255.0f)
    , g(static_cast<float>(other.g) / 255.0f)
    , b(static_cast<float>(other.b) / 255.0f)
    , a(static_cast<float>(other.a) / 255.0f)
{
}

inline std::string RgbaColor<uint8_t>::ToHexString() const
{
    char buf[10];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    return std::string(buf);
}

inline std::string RgbaColor<float>::ToHexString() const
{
    return RgbaColor<uint8_t>(*this).ToHexString();
}

/// @brief User-defined literal to convert a hex color code to an RGBA color.
constexpr RgbaColor<uint8_t> operator"" _rgba(const char* str, size_t len)
{
    auto from_hex = [](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };

    size_t offset = (len > 0 && str[0] == '#') ? 1 : 0;
    size_t digits = len - offset;

    if (digits == 3)
    {
        // Shorthand RGB (e.g., #F0A), expand to full form which is #FF00AA
        return RgbaColor<uint8_t>(
            (from_hex(str[offset]) << 4) | from_hex(str[offset]),
            (from_hex(str[offset+1]) << 4) | from_hex(str[offset+1]),
            (from_hex(str[offset+2]) << 4) | from_hex(str[offset+2])
        );
    }
    if (digits == 6)
    {
        // Full RGB (e.g., #FF00AA)
        return RgbaColor<uint8_t>(
            (from_hex(str[offset]) << 4) | from_hex(str[offset+1]),
            (from_hex(str[offset+2]) << 4) | from_hex(str[offset+3]),
            (from_hex(str[offset+4]) << 4) | from_hex(str[offset+5])
        );
    }
    if (digits == 8)
    {
        // Full RGBA (e.g., #FF00AAFF)
        return RgbaColor<uint8_t>(
            (from_hex(str[offset]) << 4) | from_hex(str[offset+1]),
            (from_hex(str[offset+2]) << 4) | from_hex(str[offset+3]),
            (from_hex(str[offset+4]) << 4) | from_hex(str[offset+5]),
            (from_hex(str[offset+6]) << 4) | from_hex(str[offset+7])
        );
    }
    return RgbaColor<uint8_t>(); // default
}

/// @brief Unique key for caching resources.
class CacheKey
{
    friend std::hash<CacheKey>;

public:
    CacheKey(std::string_view key)
        : m_Key(key)
        , m_HashCode(std::hash<std::string_view>()(key))
    {
        // CacheKey must not be empty.
        eassert(!key.empty());
    }

    bool operator==(const CacheKey& other) const
    {
        return m_HashCode == other.m_HashCode && m_Key == other.m_Key;
    }

    bool operator!=(const CacheKey& other) const
    {
        return !(*this == other);
    }

    bool operator<(const CacheKey& other) const
    {
        if(m_HashCode < other.m_HashCode)
        {
            return true;
        }
        else if(m_HashCode > other.m_HashCode)
        {
            return false;
        }
        else
        {
            return m_Key < other.m_Key;
        }
    }

    const std::string& ToString() const
    {
        return m_Key;
    }

private:
    CacheKey() = delete;

    std::string m_Key;
    size_t m_HashCode;
};

/// @brief Specification for creating a texture.
class TextureSpec
{
public:

    /// @brief Represents no texture.
    struct None_t{};

    /// @brief Constant representing no texture, used to initialize TextureSpec when no texture is desired.
    constexpr static None_t None{};

    explicit TextureSpec(None_t)
        : Source(None)
    {
    }

    /// @brief Constructs a texture spec from a file path.
    /// The cache key is set to the path.
    explicit TextureSpec(std::string_view path)
        : Source(std::string(path))
    {
    }

    /// @brief Constructs a texture spec from a color.
    TextureSpec(const RgbaColorf& color)
        : Source(color)
    {
    }

    /// @brief Returns true if the texture spec is valid (i.e., has a specified source).
    bool IsValid() const
    {
        return !std::holds_alternative<None_t>(Source);
    }

    // FIXME(KB) - add support for resource paths.
    std::variant<None_t, const std::string, const RgbaColorf> Source;

private:
    TextureSpec() = delete;
};

/// @brief Specification for creating a vertex shader.
class VertexShaderSpec
{
public:

    //FIXME(KB) - add support for embedded source code.
    //FIXME(KB) - add a cache key.
    //FIXME(KB) - add support for resource paths.
    std::variant<std::string> Source;

    const unsigned NumUniformBuffers{ 0 };
};

/// @brief Specification for creating a fragment shader.
class FragmentShaderSpec
{
public:

    //FIXME(KB) - add support for embedded source code.
    //FIXME(KB) - add a cache key.
    //FIXME(KB) - add support for resource paths.
    std::variant<std::string> Source;
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

    const VertexShaderSpec VertexShader;
    const FragmentShaderSpec FragmentShader;
};

/// @brief Material used for rendering meshes.
class Material
{
public:

    Material(
        const RgbaColorf color,
        const float metalness,
        const float roughness,
        Texture albedo,
        VertexShader vertexShader,
        FragmentShader fragmentShader)
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
    const Texture Albedo;

    /// @brief Vertex shader used by the material.
    const VertexShader VertexShader;

    /// @brief Fragment shader used by the material.
    const FragmentShader FragmentShader;

private:

    Material() = delete;
};

/// @brief Enable hashing of CacheKey for use in unordered containers.
template<>
struct std::hash<CacheKey>
{
    std::size_t operator()(const CacheKey& key) const noexcept
    {
        return key.m_HashCode;
    }
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
