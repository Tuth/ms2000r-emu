#pragma once

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#	define HAVE_SSE
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd__64__)
#	define HAVE_X86_64
#endif

#if defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	define HAVE_ARM64
#endif

// ARM64-1: the MSVC intrinsic used by the debug prints, for GCC and Clang
#if !defined(_MSC_VER)
#	define _ReturnAddress() __builtin_return_address(0)
#endif
