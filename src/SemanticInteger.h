#pragma once

#include "AssertHelper.h"

#include <limits>
#include <type_traits>

template<typename Tag, typename T = uint32_t, T InvalidValue = std::numeric_limits<T>::max()>
struct SemanticInteger
{
    static_assert(std::is_integral_v<T>, "SemanticInteger requires an integral underlying type");

    using value_type = T;

    constexpr SemanticInteger()
        : m_Value(InvalidValue)
    {
    }

    constexpr explicit SemanticInteger(T v)
        : m_Value(v)
    {
    }

    // wider-type constructor (only participates if U is integral and wider than T)
    template<typename U>
        requires(std::is_integral_v<U> && sizeof(U) > sizeof(T))
    constexpr explicit SemanticInteger(U v)
        : m_Value(static_cast<T>(v))
    {
        MLG_ASSERT(v <= std::numeric_limits<T>::max());
    }

    // unsigned -> signed (potentially narrowing)
    template<typename U>
        requires(std::is_integral_v<U> && std::is_unsigned_v<U> && std::is_signed_v<T> &&
                 (sizeof(U) >= sizeof(T)))
    constexpr explicit SemanticInteger(U v)
        : m_Value(static_cast<T>(v))
    {
        MLG_ASSERT(v <= static_cast<U>(std::numeric_limits<T>::max()));
    }

    constexpr bool IsValid() const { return m_Value != InvalidValue; }
    constexpr T Value() const { return m_Value; }

    friend constexpr bool operator==(SemanticInteger a, SemanticInteger b) = default;
    friend constexpr auto operator<=>(SemanticInteger a, SemanticInteger b) = default;

    static const SemanticInteger INVALID;

private:
    T m_Value;
};

template<typename Tag, typename T, T InvalidValue>
inline constexpr SemanticInteger<Tag, T, InvalidValue>
    SemanticInteger<Tag, T, InvalidValue>::INVALID{ InvalidValue };