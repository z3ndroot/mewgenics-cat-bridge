#pragma once

#include <stdbool.h>
#include <stdint.h>

// x86 CPUID checks.
//
// polymeric 2026

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    #define _PLATFORM_X86_64
#elif defined(__i386__) || defined(_M_IX86)
    #define _PLATFORM_X86_32
#else
    #error Non-x86 targets are not supported.
#endif

// _xgetbv
#include <immintrin.h>

#if defined(_MSC_VER)
    #include <intrin.h>
    #pragma intrinsic(__cpuidex)
    #pragma intrinsic(_xgetbv)
    #ifdef _PLATFORM_X86_32
        #pragma intrinsic(__readeflags)
        #pragma intrinsic(__writeeflags)
    #endif
#else
    #include <cpuid.h>
    #ifdef _PLATFORM_X86_32
        // __readeflags/__writeeflags
        #include <x86intrin.h>
    #endif
#endif

// Compiler attributes
#if defined(_MSC_VER)
    #define TARGET(tgt)
#else
    #define TARGET(tgt) __attribute__((target(tgt)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline bool x86_has_cpuid(void) {
    #ifdef _PLATFORM_X86_32
    static int tristate_unresolved_notpresent_present = -1;
    if(tristate_unresolved_notpresent_present < 0) {
        const uint32_t EFLAGS_ID_MASK = 1 << 21;
        uint32_t eflags_prev = __readeflags();
        __writeeflags(eflags_prev ^ EFLAGS_ID_MASK);
        uint32_t eflags_now = __readeflags();
        __writeeflags(eflags_prev);
        if(eflags_now != eflags_prev) {
            tristate_unresolved_notpresent_present = 1;
        } else {
            // The year is 1993. An interdimensional rift opens in front of a computer store,
            // dropping a single 3.5" floppy on the well-kempt surface of its parking lot.
            // You, walking home from school, notice it by chance.
            //
            // Oblivious then to the horrors of "raw-dogging" a storage device,
            // (aren't store-bought hot dogs usually pre-cooked and thus safe to eat "raw"?)
            // you pick up the floppy and race home, smacking the power switch on your
            // family's new PC (it has an Intel Pentium inside!) on your way in.
            //
            // With bated breath, you slot the diskette into the computer's floppy drive.
            // > DIR A:
            //
            // Several seconds pass...
            // "'05-07-26'... 1926? 2026? Are these files really from the future?"
            //
            // X86CPUID.H catches your attention.
            // As you page through its contents, you judge pretty harshly.
            //
            // "Is this the new 'C with classes' thing? 'C++'? Where are the classes?"
            // "What's with all this indirection??? This is 10 lines of assembly at worst!"
            //
            // "EFLAGS ID check. Guess they still make 386/486 parts, huh."
            tristate_unresolved_notpresent_present = 0;
            // "... I wonder if they have virtual assistants like in the movies by then."
        }
    }
    return tristate_unresolved_notpresent_present > 0;
    #else
    return true;
    #endif
}

static inline void x86_cpuid(uint32_t *regs, uint32_t leaf, uint32_t subleaf) {
    #ifdef _MSC_VER
    __cpuidex((int32_t *)(regs), leaf, subleaf);
    #else
    __cpuid_count(leaf, subleaf, regs[0], regs[1], regs[2], regs[3]);
    #endif
}

TARGET("xsave") static inline uint64_t x86_xgetbv(uint32_t xcr) {
    return _xgetbv(xcr);
}

static inline bool x86_has_sse2(void) {
    // Since SSE2 support is ubiquitious in the modern day, we assume SSE2 code is
    // allowed to fault if executed on an OS that uses neither XSAVE nor FXSAVE.
    #ifdef _PLATFORM_X86_32
    // ref: https://github.com/llvm/llvm-project/blob/d27d0f08078abef434f35d5feedb264663f461e8/compiler-rt/lib/builtins/cpu_model/x86.c#L890-L891
    const uint32_t CPUID_LEAF1_EDX3_SSE2_MASK = 1 << 26;
    const uint32_t CPUID_LEAF1_EDX3_REQUIRED_MASK = CPUID_LEAF1_EDX3_SSE2_MASK;

    // encode both resolved and presence info in one element to avoid store/load races between two bools
    static int tristate_unresolved_notpresent_present = -1;

    if(tristate_unresolved_notpresent_present < 0) {
        uint32_t cpuid_regs[4];

        // CPUID must be present
        if(!x86_has_cpuid()) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // CPUID must advertise at least 1 leaf
        x86_cpuid(cpuid_regs, 0, 0);
        if(cpuid_regs[0] < 1) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // CPUID Leaf 1 must indicate SSE2 support
        x86_cpuid(cpuid_regs, 1, 0);
        if((cpuid_regs[3] & CPUID_LEAF1_EDX3_REQUIRED_MASK) != CPUID_LEAF1_EDX3_REQUIRED_MASK) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        tristate_unresolved_notpresent_present = 1;
    }

    return tristate_unresolved_notpresent_present > 0;
    #else
    return true;
    #endif
}

static inline bool x86_has_avx(void) {
    // ref: https://github.com/llvm/llvm-project/blob/d27d0f08078abef434f35d5feedb264663f461e8/compiler-rt/lib/builtins/cpu_model/x86.c#L939-L940
    const uint32_t CPUID_LEAF1_ECX2_OSXSAVE_MASK = 1 << 27;
    const uint32_t CPUID_LEAF1_ECX2_AVX_MASK = 1 << 28;
    const uint32_t CPUID_LEAF1_ECX2_REQUIRED_MASK = CPUID_LEAF1_ECX2_OSXSAVE_MASK | CPUID_LEAF1_ECX2_AVX_MASK;
    const uint64_t XCR0_SSE_MASK = 1 << 1;
    const uint64_t XCR0_AVX_MASK = 1 << 2;
    const uint64_t XCR0_REQUIRED_MASK = XCR0_SSE_MASK | XCR0_AVX_MASK;

    // encode both resolved and presence info in one element to avoid store/load races between two bools
    static int tristate_unresolved_notpresent_present = -1;

    if(tristate_unresolved_notpresent_present < 0) {
        uint32_t cpuid_regs[4];

        // CPUID must be present
        if(!x86_has_cpuid()) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // CPUID must advertise at least 7 leaves
        x86_cpuid(cpuid_regs, 0, 0);
        if(cpuid_regs[0] < 7) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // CPUID Leaf 1 must indicate OSXSAVE and AVX support
        x86_cpuid(cpuid_regs, 1, 0);
        if((cpuid_regs[2] & CPUID_LEAF1_ECX2_REQUIRED_MASK) != CPUID_LEAF1_ECX2_REQUIRED_MASK) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // XFEATURE_ENABLED_MASK must indicate XMM/YMM save/restore supported + enabled
        uint64_t xcr0 = x86_xgetbv(0);
        if((xcr0 & XCR0_REQUIRED_MASK) != XCR0_REQUIRED_MASK) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        tristate_unresolved_notpresent_present = 1;
    }

    return tristate_unresolved_notpresent_present > 0;
}

static inline bool x86_has_avx2(void) {
    // ref: https://github.com/llvm/llvm-project/blob/d27d0f08078abef434f35d5feedb264663f461e8/compiler-rt/lib/builtins/cpu_model/x86.c#L954-L955
    const uint32_t CPUID_LEAF7_EBX1_AVX2_MASK = 1 << 5;
    const uint32_t CPUID_LEAF7_EBX1_REQUIRED_MASK = CPUID_LEAF7_EBX1_AVX2_MASK;

    // encode both resolved and presence info in one element to avoid store/load races between two bools
    static int tristate_unresolved_notpresent_present = -1;

    if(tristate_unresolved_notpresent_present < 0) {
        uint32_t cpuid_regs[4];

        // CPUID must be present
        if(!x86_has_cpuid()) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // CPUID must advertise at least 7 leaves
        x86_cpuid(cpuid_regs, 0, 0);
        if(cpuid_regs[0] < 7) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // Perform base checks for AVX and OS enlightenment to XSAVE XMM/YMM
        if(!x86_has_avx()) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        // CPUID Leaf 7 must indicate AVX2 support
        x86_cpuid(cpuid_regs, 7, 0);
        if((cpuid_regs[1] & CPUID_LEAF7_EBX1_REQUIRED_MASK) != CPUID_LEAF7_EBX1_REQUIRED_MASK) {
            tristate_unresolved_notpresent_present = 0;
            return false;
        }
        tristate_unresolved_notpresent_present = 1;
    }

    return tristate_unresolved_notpresent_present > 0;
}

#ifdef __cplusplus
}
#endif

#undef _PLATFORM_X86_64
#undef _PLATFORM_X86_32
#undef TARGET
