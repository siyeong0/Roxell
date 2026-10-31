#pragma once

#include <stddef.h>
#include <stdint.h>

// C-compatible scalar names for ABI headers shared by C and C++ modules.
// C cannot use the rox::core types declared in Types.h.
typedef int32_t rox_int32;
typedef int64_t rox_int64;
typedef uint32_t rox_uint32;
typedef uint64_t rox_uint64;
typedef size_t rox_usize;
