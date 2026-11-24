#pragma once

// Fixed-width integer aliases, the two failure macros, and the tiny subset of
// <utility> Nova 2 needs. Thin C-runtime headers only: no STL (design.md 2).
// From Nova 1's types.h, with i8 and i16 added for sign-extended byte reads.
#include <cstddef> // size_t
#include <cstdint> // uint8_t .. int64_t

// The code rules (design.md 2) say no exceptions and no RTTI. CMakeLists.txt
// turns both off; these make sure no build can quietly turn them back on.
#if defined(_MSC_VER) && defined(_CPPUNWIND)
#error "Nova 2 builds with exceptions off (/EHs-c-)."
#endif
#if defined(_MSC_VER) && defined(_CPPRTTI)
#error "Nova 2 builds with RTTI off (/GR-)."
#endif

namespace nova2 {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using usize = size_t;

// Prints "nova2: fatal: <msg> (<file>:<line>)" to stderr and ends the process.
// Reserved for broken invariants and out-of-memory. Bad *input* (settings,
// files) is reported through return values so tests can exercise it.
[[noreturn]] void fatal(const char* file, int line, const char* msg);

template <class T> struct remove_ref {
    using type = T;
};
template <class T> struct remove_ref<T&> {
    using type = T;
};
template <class T> struct remove_ref<T&&> {
    using type = T;
};

// Equivalent of the standard move: casts to an rvalue so move constructors
// are chosen.
template <class T> constexpr typename remove_ref<T>::type&& move(T&& v) noexcept {
    return static_cast<typename remove_ref<T>::type&&>(v);
}

// Named min_of/max_of so windows.h's min/max macros can never collide.
template <class T> constexpr T min_of(T a, T b) {
    return a < b ? a : b;
}
template <class T> constexpr T max_of(T a, T b) {
    return a < b ? b : a;
}

} // namespace nova2

#define NOVA2_FATAL(msg) ::nova2::fatal(__FILE__, __LINE__, (msg))

// Inlined whatever the compiler's own judgment says: for the few small
// functions the loops that run instructions call for every one of them, which
// the compiler otherwise leaves as calls (optimization-plan.md 4.2).
#define NOVA2_INLINE __forceinline

#ifdef NDEBUG
#define NOVA2_ASSERT(expr) ((void)0)
#else
#define NOVA2_ASSERT(expr)                                                                         \
    ((expr) ? (void)0 : ::nova2::fatal(__FILE__, __LINE__, "assertion failed: " #expr))
#endif
