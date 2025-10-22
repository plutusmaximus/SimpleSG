#pragma once

#include <string_view>

template<typename T>
struct RgbaColor
{
    RgbaColor()
        : RgbaColor(0, 0, 0, 0)
    {
    }

    RgbaColor(T inR, T inG, T inB)
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    RgbaColor(T inR, T inG, T inB, T inA)
        : r(inR), g(inG), b(inB), a(inA)
    {
    }

    T r, g, b, a;
};

using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

//Specialization for uint8_t to use 255 has max value.
inline RgbaColor<uint8_t>::RgbaColor(uint8_t inR, uint8_t inG, uint8_t inB)
    : RgbaColor<uint8_t>(inR, inG, inB, 255)
{
}

class MaterialId
{
    static constexpr unsigned INVALID_VALUE = 0;

public:

    MaterialId()
        : m_Value(INVALID_VALUE)
    {
    }

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

        return MaterialId(next);
    }

private:

    unsigned m_Value;

    explicit MaterialId(unsigned value) : m_Value(value) {}
};

struct MaterialSpec
{
    RgbaColorf Color;

    std::string_view VertexShader;
    std::string_view FragmentShader;

    float Metallic{ 0 };
    float Roughness{ 0 };

    std::string_view Albedo;
};