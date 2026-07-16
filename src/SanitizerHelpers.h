#pragma once

#if defined(__has_feature) && defined(__has_include)
#  if __has_feature(leak_sanitizer) && __has_include(<sanitizer/lsan_interface.h>)
#include <sanitizer/lsan_interface.h>
#    define MLG_LSAN_IGNORE_OBJECT(x) __lsan_ignore_object(x)
#  else
#    define MLG_LSAN_IGNORE_OBJECT(x) (void)(x)
#  endif
#else
#  define MLG_LSAN_IGNORE_OBJECT(x) (void)(x)
#endif