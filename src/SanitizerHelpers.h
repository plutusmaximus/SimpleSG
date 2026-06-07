#pragma once

#define MLG_HAS_ASAN 0

#if defined(__SANITIZE_ADDRESS__) || defined(__has_feature)
#  if defined(__has_feature)
#    if __has_feature(address_sanitizer)
#      define MLG_HAS_ASAN 1
#    endif
#  endif
#  if defined(__SANITIZE_ADDRESS__)
#    define MLG_HAS_ASAN 1
#  endif
#endif

#if MLG_HAS_ASAN
#include <sanitizer/lsan_interface.h>
#else
#  define __lsan_ignore_object(x) (void)(x) // NOLINT(bugprone-reserved-identifier)
#endif