#pragma once

#include <string>
#include <algorithm>

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

    const std::string Albedo;

    const std::string VertexShader;
    const std::string FragmentShader;
};

class Material
{
public:

    /// @brief Unique key identifying this material.
    /// Used to group geometry sharing the same material attributes.
    const MaterialKey Key;

    const RgbaColorf Color;

    const float Metallic{ 0 };
    const float Roughness{ 0 };

    RefPtr<GpuTexture> const Albedo;

    RefPtr<GpuVertexShader> const VertexShader;
    RefPtr<GpuFragmentShader> const FragmentShader;
};

namespace std
{
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