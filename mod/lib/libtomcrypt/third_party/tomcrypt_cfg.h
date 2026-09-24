/* LibTomCrypt, modular cryptographic library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */

/* This is the build config file.
 *
 * With this you can setup what to include/exclude automatically during any build.  Just comment
 * out the line that #define's the word for the thing you want to remove.  phew!
 */

#ifndef TOMCRYPT_CFG_H
#define TOMCRYPT_CFG_H

/* some compilers do not like "inline" (or maybe "static inline") */
#if defined(__GNUC__)
   #define LTC_INLINE __inline__
#elif defined(_MSC_VER)
   #define LTC_INLINE __inline
#else
   #define LTC_INLINE inline
#endif

/* Controls endianess and size of registers. */
/* detect x86/i386/ARM 32bit */
#if defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_M_ARM)
   #define ENDIAN_LITTLE
   #define ENDIAN_32BITWORD
#endif

/* detect amd64/x64/arm64 */
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64) || defined(_M_ARM64)
   #define ENDIAN_LITTLE
   #define ENDIAN_64BITWORD
#endif

/* endianness fallback */
#if !defined(ENDIAN_BIG) && !defined(ENDIAN_LITTLE)
  #error Cannot detect endianness
#endif

/* ulong64: 64-bit data type */
#ifdef _MSC_VER
   #define CONST64(n) n ## ui64
   typedef unsigned __int64 ulong64;
   typedef __int64 long64;
#else
   #define CONST64(n) n ## uLL
   typedef unsigned long long ulong64;
   typedef long long long64;
#endif

/* ulong32: "32-bit at least" data type */
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64) || defined(_M_ARM64)
   typedef unsigned ulong32;
#else
   typedef unsigned long ulong32;
#endif

#if (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
   #define LTC_ARCH_X86
#endif

#ifdef LTC_ARCH_X86
   #if (defined __GNUC__ && (__GNUC__ * 100 + __GNUC_MINOR__ >= 409)) || \
       (defined __clang__ && (__clang_major__ * 100 + __clang_minor__ >= 308)) || \
       (defined _MSC_VER && defined _MSC_FULL_VER && (_MSC_VER) >= 1900)
      #if !defined(LTC_NO_SHA256_X86)
         #define LTC_SHA256_X86
      #endif
   #endif
#endif /* LTC_ARCH_X86 */

#if defined(__GNUC__)
   #define LTC_ALIGN_MSVC(n)
   #define LTC_ALIGN(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
   #define LTC_ALIGN_MSVC(n) __declspec(align(n))
   #define LTC_ALIGN(n)
#else
   #define LTC_ALIGN_MSVC(n)
   #define LTC_ALIGN(n)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define LTC_ATTRIBUTE(x) __attribute__(x)
#else
#  define LTC_ATTRIBUTE(x)
#endif

#endif /* TOMCRYPT_CFG_H */
