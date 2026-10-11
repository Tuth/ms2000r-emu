#pragma once
// LINUX-1: MSVC keywords and CRT functions used by the sources, for GCC/Clang. Force-included by
// CMakeLists.txt for non-MSVC compilers only; MSVC never sees this file.
#ifndef _MSC_VER
#include <cstdio>
#include <cerrno>
#include <cstddef>
#include <ctime>

#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif

inline int fopen_s(FILE** f, const char* name, const char* mode)
{
    *f = std::fopen(name, mode);
    return *f ? 0 : errno;
}

// MSVC sprintf_s: the array form and the (buffer, size) form.
#include <cstdarg>
inline int sprintf_s(char* buf, std::size_t size, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    const int n = std::vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
template<std::size_t N, typename... A>
inline int sprintf_s(char (&buf)[N], const char* fmt, A... args) { return std::snprintf(buf, N, fmt, args...); }

// MSVC argument order: (result, time) - the POSIX localtime_r has them the other way round.
inline int localtime_s(std::tm* out, const std::time_t* t) { return localtime_r(t, out) ? 0 : errno; }

// MSVC strncpy_s(dest, destSize, src, count): copies at most count chars and always terminates.
inline int strncpy_s(char* dest, std::size_t destSize, const char* src, std::size_t count)
{
    if (!dest || !destSize) return EINVAL;
    std::size_t n = 0;
    while (n < count && n + 1 < destSize && src[n]) { dest[n] = src[n]; ++n; }
    dest[n] = 0;
    return 0;
}
#endif
