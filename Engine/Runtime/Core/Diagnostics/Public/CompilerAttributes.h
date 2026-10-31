#pragma once

// Use on measured hot template boundaries and small accessors. Inlining is not guaranteed,
// does not remove checks or add optimizer assumptions, and can increase code size.
#if defined(_MSC_VER)
#define ROXELL_FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define ROXELL_FORCE_INLINE inline __attribute__((always_inline))
#else
#define ROXELL_FORCE_INLINE inline
#endif

// Keep measured growth paths outside callers that have reserved capacity.
#if defined(_MSC_VER)
#define ROXELL_NOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
#define ROXELL_NOINLINE __attribute__((noinline))
#else
#define ROXELL_NOINLINE
#endif

// Hint that a cache line will be needed for a predictable random access.
#if defined(_MSC_VER)
#include <intrin.h>
#define ROXELL_PREFETCH(address) _mm_prefetch(reinterpret_cast<const char*>(address), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define ROXELL_PREFETCH(address) __builtin_prefetch(address)
#else
#define ROXELL_PREFETCH(address) static_cast<void>(address)
#endif
