#pragma once

#include <AssertHelper.h>

#include <cstdint>
#include <type_traits>
#include <limits>

template<typename T, typename U>
inline T narrow_cast(U u) noexcept
{
    static_assert(std::is_integral_v<T> && std::is_integral_v<U>, "narrow_cast requires integral types");
    static_assert((std::numeric_limits<T>::is_signed == std::numeric_limits<U>::is_signed) ||
                      (std::numeric_limits<T>::is_signed && !std::numeric_limits<U>::is_signed),
        "narrow_cast requires both types to have the same signedness, or the destination type to be signed and the source type to be unsigned");
    static_assert(std::numeric_limits<T>::digits <= std::numeric_limits<U>::digits,
        "narrow_cast requires the destination type to have fewer or the same digits than the source type");
    const T t = static_cast<T>(u);
    MLG_ASSERT(static_cast<U>(t) == u, "narrow_cast failed: {} -> {}", u, t);
    return t;
}