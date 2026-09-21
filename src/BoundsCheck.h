#pragma once

#include "AssertHelper.h"

#include <cstddef>
#include <type_traits>
#include <limits>

class BoundsCheck
{
public:

    template<typename T, typename U, typename V>
    static T Sum(const T curValue, const U addend, const V inclusiveMaxValue)
    {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>,
            "T must be an unsigned integral type");
        static_assert(std::is_integral_v<U> && std::is_unsigned_v<U>,
            "U must be an unsigned integral type");
        static_assert(std::is_integral_v<V> && std::is_unsigned_v<V>,
            "V must be an unsigned integral type");

        constexpr T maxT = std::numeric_limits<T>::max();

        MLG_ABORTIF(curValue > inclusiveMaxValue, "Current value out of bounds");
        MLG_ABORTIF(addend > inclusiveMaxValue, "Addend out of bounds");
        MLG_ABORTIF(inclusiveMaxValue - curValue < addend, "Sum out of bounds");

        MLG_ABORTIF(curValue > maxT, "Current value out of bounds");
        MLG_ABORTIF(addend > maxT, "Addend out of bounds");
        MLG_ABORTIF(maxT - curValue < addend, "Sum out of bounds");

        return static_cast<T>(curValue + addend);
    }

    /// Check an index against a maximum value and return it as type T.
    template<typename T = size_t, typename U, typename V>
    static T Index(const U index, const V exclusiveMaxValue)
    {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>,
            "T must be an unsigned integral type");
        static_assert(std::is_integral_v<U> && std::is_unsigned_v<U>,
            "U must be an unsigned integral type");
        static_assert(std::is_integral_v<V> && std::is_unsigned_v<V>,
            "V must be an unsigned integral type");

        MLG_ABORTIF(exclusiveMaxValue > std::numeric_limits<T>::max(), "Max value out of bounds");
        MLG_ABORTIF(index >= exclusiveMaxValue, "Index out of bounds");
        return static_cast<T>(index);
    }

    template<typename T = size_t, typename U, typename V, typename W>
    static T Count(const U offset, const V count, const W inclusiveMaxValue)
    {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>,
            "T must be an unsigned integral type");
        static_assert(std::is_integral_v<U> && std::is_unsigned_v<U>,
            "U must be an unsigned integral type");
        static_assert(std::is_integral_v<V> && std::is_unsigned_v<V>,
            "V must be an unsigned integral type");
        static_assert(std::is_integral_v<W> && std::is_unsigned_v<W>,
            "W must be an unsigned integral type");

        constexpr T maxT = std::numeric_limits<T>::max();

        MLG_ABORTIF(offset >= inclusiveMaxValue, "Offset out of bounds");
        MLG_ABORTIF(count > inclusiveMaxValue, "Count out of bounds");
        MLG_ABORTIF(inclusiveMaxValue - offset < count, "Range out of bounds");

        MLG_ABORTIF(offset >= maxT, "Offset out of bounds");
        MLG_ABORTIF(count > maxT, "Count out of bounds");
        MLG_ABORTIF(maxT - offset < count, "Range out of bounds");

        return static_cast<T>(count);
    }
};
