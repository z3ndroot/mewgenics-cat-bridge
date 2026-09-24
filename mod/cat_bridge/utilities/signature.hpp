#pragma once

// Enable use of SIMD intrinsics, to accelerate pattern scanning
#define USE_SSE2_INTRINSICS_STAGE1
#define USE_AVX2_INTRINSICS_STAGE1
#define USE_SSE2_INTRINSICS_STAGE2
#define USE_AVX2_INTRINSICS_STAGE2

// #include "utilities/debug_console.hpp"
#include "utilities/constexpr.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <cstring>
#include <format>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

// Signature descriptors and memory pattern scanning.
//
// "CATS: ALL YOUR BASE ARE BELONG TO US."
// "Captain: You know what you doing. Take off every 'SIG'!"
//
// polymeric 2026

#if defined(USE_SSE2_INTRINSICS_STAGE1) || defined(USE_SSE2_INTRINSICS_STAGE2) || defined(USE_AVX2_INTRINSICS_STAGE1) || defined(USE_AVX2_INTRINSICS_STAGE2)
    #include "utilities/x86_cpuid.h"
    #include <immintrin.h>
#endif

#if defined(USE_SSE2_INTRINSICS_STAGE1) || defined(USE_AVX2_INTRINSICS_STAGE1)
    #if defined(_MSC_VER)
        #include <intrin.h>
        #pragma intrinsic(_BitScanForward)
    #endif
#endif

// Compiler attributes
#if defined(_MSC_VER)
    #define MUST_INLINE __forceinline
#else
    #define MUST_INLINE inline __attribute__((always_inline))
#endif
#if defined(__GNUC__) || defined(__clang__)
    // ClangCL also needs this decoration
    #define TARGET(tgt) __attribute__((target(tgt)))
#else
    #define TARGET(tgt)
#endif

class BPatternDescriptor {
public:
    size_t first_nonwildcard_idx;
    size_t last_nonwildcard_idx;
    bool trivial_pattern; // empty or all wildcards

    virtual ~BPatternDescriptor() = default;
    virtual std::span<const uint8_t> pattern() const = 0;
    virtual std::span<const uint8_t> pattern_mask() const = 0;

    const uint8_t *find_unique_match_or_none(const uint8_t *seq_start, size_t seq_size_bytes) const {
        const uint8_t *match = nullptr;
        find_callback(seq_start, seq_size_bytes, [&](const uint8_t *result) -> bool {
            if(match == nullptr) {
                match = result;
                return true;
            } else {
                // invalidate the last finding as it wasn't a unique match
                match = nullptr;
                return false;
            }
        });
        return match;
    }

    template<std::predicate<const uint8_t *> CB>
    void find_callback(const uint8_t *seq_start, size_t seq_size_bytes, CB &&callback) const {
        // Trivial cases.
        size_t pattern_size_bytes = this->pattern().size_bytes();
        if(pattern_size_bytes > seq_size_bytes) {
            // a pattern will never match a sequence of shorter length
            return;
        }
        size_t num_search_positions = seq_size_bytes - pattern_size_bytes + 1;
        if(this->trivial_pattern) {
            // pattern_size_bytes > seq_size_bytes:
            // - untraversable due to check above
            // pattern_size_bytes < seq_size_bytes:
            // - an all-wildcard or empty pattern trivially matches a sequence of longer length at more than one point
            // pattern_size_bytes == seq_size_bytes:
            // - an all-wildcard pattern trivially matches a sequence of equivalent length at offset 0
            // - 'the' empty pattern matches 'the' empty sequence at offset 0
            for(size_t i = 0; i < num_search_positions; i++) {
                if(!callback(seq_start + i)) {
                    return;
                }
            }
            return;
        }

        // Non-trivial cases. The following preconditions are assured:
        // - 1 <= pattern.pattern.size_bytes() <= size_bytes
        // - the pattern is not all wildcards

        size_t dist_pattern_first_last_nonwildcard = this->last_nonwildcard_idx - this->first_nonwildcard_idx;

        // The core search algorithm marches along the sequence,
        // testing the first and last non-wildcard bytes (Stage 1),
        // and cascades into a full string comparison whenever both match (Stage 2).

        // We specialize Stage 2 to reduce overhead from redundant pattern length checks in the hot loop.
        // To allow the C++ compiler to generate static code paths, we template both Stage 1 and Stage 2.
        auto stage1_compare = [&]<bool S2SSE2, bool S2AVX2>() {
            const uint8_t *pattern_ = this->pattern().data();
            const uint8_t *pattern_mask_ = this->pattern_mask().data();

            // adjust start to base indices on first_nonwildcard_idx
            size_t offset = this->first_nonwildcard_idx;
            // and end to avoid overrunning buffers beyond pattern.pattern.size_bytes()
            size_t limit = offset + num_search_positions;

            // ref: http://0x80.pl/notesen/2016-11-28-simd-strfind.html#generic-sse-avx2
            #ifdef USE_AVX2_INTRINSICS_STAGE1
            if(x86_has_avx2()) {
                if(!this->stage1_compare_avx2_loop<S2SSE2, S2AVX2>(
                    seq_start,
                    callback,
                    &offset,
                    limit,
                    dist_pattern_first_last_nonwildcard,
                    pattern_, pattern_mask_
                )) {
                    return;
                }
            }
            #endif

            #ifdef USE_SSE2_INTRINSICS_STAGE1
            if(x86_has_sse2()) {
                if(!stage1_compare_sse2_loop<S2SSE2, S2AVX2>(
                    seq_start,
                    callback,
                    &offset,
                    limit,
                    dist_pattern_first_last_nonwildcard,
                    pattern_, pattern_mask_
                )) {
                    return;
                }
            }
            #endif

            while(offset < limit) {
                const uint8_t *addr = seq_start + offset;
                bool first_byte_matches = (addr[0] & pattern_mask_[this->first_nonwildcard_idx]) == pattern_[this->first_nonwildcard_idx];
                bool last_byte_matches = (addr[dist_pattern_first_last_nonwildcard] & pattern_mask_[this->last_nonwildcard_idx]) == pattern_[this->last_nonwildcard_idx];

                if(first_byte_matches && last_byte_matches) {
                    if(stage2_compare<S2SSE2, S2AVX2>(
                        addr,
                        &pattern_[this->first_nonwildcard_idx],
                        &pattern_mask_[this->first_nonwildcard_idx],
                        dist_pattern_first_last_nonwildcard + 1
                    )) {
                        const uint8_t *match_start = addr - this->first_nonwildcard_idx;
                        if(!callback(match_start)) {
                            return;
                        }
                    }
                }
                offset++;
            }
        };

        // Then we invoke one of the templated lambdas with selected Stage 2 parameters.
        #ifdef USE_AVX2_INTRINSICS_STAGE2
        const bool stage2_has_avx2 = x86_has_avx2();
        #else
        const bool stage2_has_avx2 = false;
        #endif
        #ifdef USE_SSE2_INTRINSICS_STAGE2
        const bool stage2_has_sse2 = x86_has_sse2();
        #else
        const bool stage2_has_sse2 = false;
        #endif
        if(dist_pattern_first_last_nonwildcard + 1 >= 32) {
            if(stage2_has_avx2) {
                stage1_compare.template operator()<true, true>();
            } else if(stage2_has_sse2) {
                stage1_compare.template operator()<true, false>();
            } else {
                stage1_compare.template operator()<false, false>();
            }
        } else if(dist_pattern_first_last_nonwildcard + 1 >= 16) {
            if(stage2_has_sse2) {
                stage1_compare.template operator()<true, false>();
            } else {
                stage1_compare.template operator()<false, false>();
            }
        } else {
            stage1_compare.template operator()<false, false>();
        }
    }

    std::string to_string() const {
        std::string builder;
        builder += "(pattern: ";
        size_t size = this->pattern().size();
        for(size_t i = 0; i < size; i++) {
            uint8_t patt = this->pattern()[i];
            uint8_t mask = this->pattern_mask()[i];
            if(mask >> 4 == 0xF) {
                builder.push_back("0123456789ABCDEF"[patt >> 4]);
            } else if(mask >> 4 == 0x0) {
                builder.push_back('?');
            } else {
                builder.push_back('/');
            }
            if((mask & 0xF) == 0xF) {
                builder.push_back("0123456789ABCDEF"[patt & 0xF]);
            } else if((mask & 0xF) == 0x0) {
                builder.push_back('?');
            } else {
                builder.push_back('/');
            }
            // if(i < size - 1) {
            //     builder.push_back(' ');
            // }
        }
        builder += std::format(", size {}, 1nwi: {}, -1nwi: {}, trivial: {})", size, this->first_nonwildcard_idx, this->last_nonwildcard_idx, this->trivial_pattern);
        return builder;
    }

protected:
    static constexpr size_t make_pattern_calc_size(const std::string_view sv) {
        size_t cnt = 0;
        for(size_t i = 0; i < sv.length(); i++) {
            if(sv[i] != ' ' && sv[i] != '\t') {
                cnt++;
            }
        }
        if(cnt % 2 != 0) { // odd
            // throwing in a constexpr context is very cursed, but as they say, "when in Rome"
            throw std::logic_error("Given hex pattern does not have an even number of digits");
        }
        return cnt / 2;
    }

    constexpr void make_pattern_compile(const std::string_view sv, const size_t size, std::span<uint8_t> pattern_impl, std::span<uint8_t> pattern_mask_impl) {
        this->first_nonwildcard_idx = size;
        this->last_nonwildcard_idx = size;
        this->trivial_pattern = true;

        size_t cnt = 0;
        for(size_t i = 0; i < sv.length(); i++) {
            if(sv[i] != ' ' && sv[i] != '\t') { // horizontal whitespace
                if(sv[i] != '?' && parse_char_0_to_F_as_hex(sv[i]) >= 16) { // ?, 0-9, A-F, a-f
                    throw std::logic_error("Given hex pattern has unexpected characters");
                }
                if(cnt % 2 == 0) { // nibble 0, high
                    pattern_impl[cnt / 2] = 0x00;
                    pattern_mask_impl[cnt / 2] = 0xFF;
                    if(sv[i] == '?') {
                        pattern_mask_impl[cnt / 2] ^= 0xF0;
                    } else {
                        pattern_impl[cnt / 2] |= parse_char_0_to_F_as_hex(sv[i]) << 4;
                        if(this->first_nonwildcard_idx == size) {
                            this->first_nonwildcard_idx = cnt / 2;
                        }
                        this->last_nonwildcard_idx = cnt / 2;
                        this->trivial_pattern = false;
                    }
                } else { // nibble 1, low
                    if(sv[i] == '?') {
                        pattern_mask_impl[cnt / 2] ^= 0x0F;
                    } else {
                        pattern_impl[cnt / 2] |= parse_char_0_to_F_as_hex(sv[i]);
                        if(this->first_nonwildcard_idx == size) {
                            this->first_nonwildcard_idx = cnt / 2;
                        }
                        this->last_nonwildcard_idx = cnt / 2;
                        this->trivial_pattern = false;
                    }
                }
                cnt++;
            }
            // mutual guarantee with make_pattern_calc_size: cnt will never exceed size here
        }
        // mutual guarantee with make_pattern_calc_size: cnt will equal size here
    }

private:
    #if defined(USE_SSE2_INTRINSICS_STAGE1) || defined(USE_AVX2_INTRINSICS_STAGE1)
    static MUST_INLINE uint32_t bsf(uint32_t mask) {
        uint32_t bitpos;
        #ifdef _MSC_VER
        static_assert(sizeof(uint32_t) == sizeof(unsigned long));
        _BitScanForward(reinterpret_cast<unsigned long *>(&bitpos), mask);
        #else
        bitpos = __builtin_ctz(mask);
        #endif
        return bitpos;
    }

    template<bool S2SSE2, bool S2AVX2, std::predicate<uint8_t *> CB>
    MUST_INLINE bool stage1_compare_bsf_inner_loop(
        CB &&callback,
        size_t dist_pattern_first_last_nonwildcard,
        const uint8_t *pattern_,
        const uint8_t *pattern_mask_,
        const uint8_t *addr,
        uint32_t equality_bytewise
    ) const {
        while(equality_bytewise != 0) {
            // get rightmost set index (lower indices first)
            uint32_t bitpos = bsf(equality_bytewise);

            if(stage2_compare<S2SSE2, S2AVX2>(
                addr + bitpos,
                &pattern_[this->first_nonwildcard_idx],
                &pattern_mask_[this->first_nonwildcard_idx],
                dist_pattern_first_last_nonwildcard + 1
            )) {
                const uint8_t *match_start = addr + bitpos - this->first_nonwildcard_idx;
                if(!callback(match_start)) {
                    return false;
                }
            }

            // clear rightmost set
            equality_bytewise &= equality_bytewise - 1;
        }
        return true;
    }
    #endif

    #ifdef USE_AVX2_INTRINSICS_STAGE1
    template<bool S2SSE2, bool S2AVX2, std::predicate<uint8_t *> CB>
    TARGET("avx2") bool stage1_compare_avx2_loop(
        const uint8_t *seq_start,
        CB &&callback,
        size_t *p_offset,
        size_t limit,
        size_t dist_pattern_first_last_nonwildcard,
        const uint8_t *pattern_,
        const uint8_t *pattern_mask_
    ) const {
        const __m256i vec_first_byte = _mm256_set1_epi8(pattern_[this->first_nonwildcard_idx]);
        const __m256i vec_last_byte = _mm256_set1_epi8(pattern_[this->last_nonwildcard_idx]);
        const __m256i vec_first_byte_mask = _mm256_set1_epi8(pattern_mask_[this->first_nonwildcard_idx]);
        const __m256i vec_last_byte_mask = _mm256_set1_epi8(pattern_mask_[this->last_nonwildcard_idx]);
        while(*p_offset + 32 <= limit) {
            const uint8_t *addr = seq_start + *p_offset;
            const __m256i vec_first_block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(addr));
            const __m256i vec_last_block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(addr + dist_pattern_first_last_nonwildcard));

            const __m256i vec_eq_first_byte = _mm256_cmpeq_epi8(vec_first_byte, _mm256_and_si256(vec_first_byte_mask, vec_first_block));
            const __m256i vec_eq_last_byte = _mm256_cmpeq_epi8(vec_last_byte, _mm256_and_si256(vec_last_byte_mask, vec_last_block));

            const __m256i vec_eq_both_bytes = _mm256_and_si256(vec_eq_first_byte, vec_eq_last_byte);
            uint32_t equality_bytewise = _mm256_movemask_epi8(vec_eq_both_bytes);

            if(!this->stage1_compare_bsf_inner_loop<S2SSE2, S2AVX2>(
                callback,
                dist_pattern_first_last_nonwildcard,
                pattern_, pattern_mask_,
                addr,
                equality_bytewise
            )) {
                return false;
            }

            *p_offset += 32;
        }
        return true;
    }
    #endif

    #ifdef USE_SSE2_INTRINSICS_STAGE1
    template<bool S2SSE2, bool S2AVX2, std::predicate<uint8_t *> CB>
    TARGET("sse2") bool stage1_compare_sse2_loop(
        const uint8_t *seq_start,
        CB &&callback,
        size_t *p_offset,
        size_t limit,
        size_t dist_pattern_first_last_nonwildcard,
        const uint8_t *pattern_,
        const uint8_t *pattern_mask_
    ) const {
        const __m128i vec_first_byte = _mm_set1_epi8(pattern_[this->first_nonwildcard_idx]);
        const __m128i vec_last_byte = _mm_set1_epi8(pattern_[this->last_nonwildcard_idx]);
        const __m128i vec_first_byte_mask = _mm_set1_epi8(pattern_mask_[this->first_nonwildcard_idx]);
        const __m128i vec_last_byte_mask = _mm_set1_epi8(pattern_mask_[this->last_nonwildcard_idx]);
        while(*p_offset + 16 <= limit) {
            const uint8_t *addr = seq_start + *p_offset;
            const __m128i vec_first_block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(addr));
            const __m128i vec_last_block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(addr + dist_pattern_first_last_nonwildcard));

            const __m128i vec_eq_first_byte = _mm_cmpeq_epi8(vec_first_byte, _mm_and_si128(vec_first_byte_mask, vec_first_block));
            const __m128i vec_eq_last_byte = _mm_cmpeq_epi8(vec_last_byte, _mm_and_si128(vec_last_byte_mask, vec_last_block));

            const __m128i vec_eq_both_bytes = _mm_and_si128(vec_eq_first_byte, vec_eq_last_byte);

            uint32_t equality_bytewise = _mm_movemask_epi8(vec_eq_both_bytes);

            if(!this->stage1_compare_bsf_inner_loop<S2SSE2, S2AVX2>(
                callback,
                dist_pattern_first_last_nonwildcard,
                pattern_, pattern_mask_,
                addr,
                equality_bytewise
            )) {
                return false;
            }

            *p_offset += 16;
        }
        return true;
    }
    #endif

    #ifdef USE_AVX2_INTRINSICS_STAGE2
    TARGET("avx2") static bool stage2_compare_avx2_loop(size_t *p_offset, const uint8_t *ptr_0, const uint8_t *ptr_1, const uint8_t *ptr_mask, size_t size_bytes) {
        const __m256i vec_zero = _mm256_setzero_si256();
        while(*p_offset + 32 <= size_bytes) {
            const __m256i vec_0 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(ptr_0 + *p_offset));
            const __m256i vec_1 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(ptr_1 + *p_offset));
            // bitwise difference acceptance mask
            const __m256i vec_mask = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(ptr_mask + *p_offset));

            // bitwise difference vector
            const __m256i vec_difference = _mm256_xor_si256(vec_0, vec_1);

            // If bit i has different values and the mask bit is set, then there is a miscompare.
            // miscompares[bit_i] = vec_mask[bit_i] && vec_difference[bit_i]
            const __m256i vec_miscompares_bitwise = _mm256_and_si256(vec_mask, vec_difference);

            // bytewise equality vector
            const __m256i vec_equality_bytewise = _mm256_cmpeq_epi8(vec_miscompares_bitwise, vec_zero);

            uint32_t equality_bytewise = _mm256_movemask_epi8(vec_equality_bytewise);

            if(equality_bytewise != 0xFFFFFFFF) {
                return false;
            }

            *p_offset += 32;
        }
        return true;
    }
    #endif

    #ifdef USE_SSE2_INTRINSICS_STAGE2
    TARGET("sse2") static bool stage2_compare_sse2_loop(size_t *p_offset, const uint8_t *ptr_0, const uint8_t *ptr_1, const uint8_t *ptr_mask, size_t size_bytes) {
        const __m128i vec_zero = _mm_setzero_si128();
        while(*p_offset + 16 <= size_bytes) {
            const __m128i vec_0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr_0 + *p_offset));
            const __m128i vec_1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr_1 + *p_offset));
            // bitwise difference acceptance mask
            const __m128i vec_mask = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr_mask + *p_offset));

            // bitwise difference vector
            const __m128i vec_difference = _mm_xor_si128(vec_0, vec_1);

            // If bit i has different values and the mask bit is set, then there is a miscompare.
            // miscompares[bit_i] = vec_mask[bit_i] && vec_difference[bit_i]
            const __m128i vec_miscompares_bitwise = _mm_and_si128(vec_mask, vec_difference);

            // bytewise equality vector
            const __m128i vec_equality_bytewise = _mm_cmpeq_epi8(vec_miscompares_bitwise, vec_zero);

            int equality_bytewise = _mm_movemask_epi8(vec_equality_bytewise);

            if(equality_bytewise != 0xFFFF) {
                return false;
            }

            *p_offset += 16;
        }
        return true;
    }
    #endif

    template<bool S2SSE2, bool S2AVX2>
    static bool stage2_compare(const uint8_t *ptr_0, const uint8_t *ptr_1, const uint8_t *ptr_mask, size_t size_bytes) {
        size_t offset = 0;

        // Stage 2 optimizations involve a time tradeoff between rejecting short patterns vs. matching long patterns.
        // Our strategy is to avoid entering SIMD loops if we know that the pattern is too short to fit in a wide register.
        #ifdef USE_AVX2_INTRINSICS_STAGE2
        if constexpr(S2AVX2) {
            if(!stage2_compare_avx2_loop(&offset, ptr_0, ptr_1, ptr_mask, size_bytes)) {
                return false;
            }
        }
        #endif

        #ifdef USE_SSE2_INTRINSICS_STAGE2
        if constexpr(S2SSE2) {
            if(!stage2_compare_sse2_loop(&offset, ptr_0, ptr_1, ptr_mask, size_bytes)) {
                return false;
            }
        }
        #endif

        while(offset < size_bytes) {
            uint8_t difference = ptr_0[offset] ^ ptr_1[offset];
            uint8_t miscompares = ptr_mask[offset] & difference;
            if(miscompares != 0) {
                return false;
            }
            offset++;
        }

        return true;
    }
};

// ArrayPatternDescriptors are meant to be instantiated in a constexpr context
template<size_t S>
class ArrayPatternDescriptor : public BPatternDescriptor {
public:
    constexpr ArrayPatternDescriptor(const std::string_view sv) {
        make_pattern_compile(sv, S, this->pattern_impl, this->pattern_mask_impl);
    }

    std::span<const uint8_t> pattern() const override {
        return this->pattern_impl;
    }

    std::span<const uint8_t> pattern_mask() const override {
        return this->pattern_mask_impl;
    }

private:
    std::array<uint8_t, S> pattern_impl;
    std::array<uint8_t, S> pattern_mask_impl;
};

// VectorPatternDescriptors are meant to be instantiated at runtime
class VectorPatternDescriptor : public BPatternDescriptor {
public:
    VectorPatternDescriptor(const std::string_view sv) {
        size_t size = make_pattern_calc_size(sv);
        this->pattern_impl.resize(size);
        this->pattern_mask_impl.resize(size);

        make_pattern_compile(sv, size, this->pattern_impl, this->pattern_mask_impl);
    }

    std::span<const uint8_t> pattern() const override {
        return std::span<const uint8_t>(this->pattern_impl);
    }

    std::span<const uint8_t> pattern_mask() const override {
        return std::span<const uint8_t>(this->pattern_mask_impl);
    }

private:
    std::vector<uint8_t> pattern_impl;
    std::vector<uint8_t> pattern_mask_impl;
};

// inherit to access protected make_pattern_calc_size from BPatternDescriptor
class PatternDescriptor : BPatternDescriptor {
public:
    PatternDescriptor() = delete;

    template<FixedString FS>
    static consteval auto make() {
        constexpr size_t S = make_pattern_calc_size(FS);
        ArrayPatternDescriptor<S> pd(FS);
        return pd;
    }

    static VectorPatternDescriptor make(const std::string_view sv) {
        VectorPatternDescriptor pd(sv);
        return pd;
    }
};

class ISigDescriptor {
public:
    using SeqToVaCb = std::function<const uint8_t *(const uint8_t *addr)>;
    virtual ~ISigDescriptor() = default;
    virtual const uint8_t *find_unique_match_or_none(const uint8_t *seq_start, size_t seq_size_bytes, const SeqToVaCb &seq_to_va) const = 0;
};

template<typename PD>
struct BDirectSig : ISigDescriptor {
    PD pattern;
    ptrdiff_t offset;

    constexpr BDirectSig(PD pattern, ptrdiff_t offset) :
        pattern(std::move(pattern)), offset(offset)
    {}

    const uint8_t *find_unique_match_or_none(const uint8_t *seq_start, size_t seq_size_bytes, const SeqToVaCb &seq_to_va) const override {
        const uint8_t *addr_va = seq_to_va(this->pattern.find_unique_match_or_none(seq_start, seq_size_bytes));
        if(addr_va == nullptr) {
            return nullptr;
        } else {
            return addr_va + offset;
        }
    }
};

class DirectSig {
public:
    DirectSig() = delete;

    template<FixedString FS>
    static consteval auto make(ptrdiff_t offset) {
        auto pd = PatternDescriptor::make<FS>();
        return BDirectSig(pd, offset);
    }

    static BDirectSig<VectorPatternDescriptor> make(const std::string_view sv, ptrdiff_t offset) {
        VectorPatternDescriptor pd = PatternDescriptor::make(sv);
        return BDirectSig(pd, offset);
    }
};

template<typename PD>
struct BIndirectSig : ISigDescriptor {
    PD pattern;
    ptrdiff_t offset;
    uint8_t length;
    bool signed_;
    bool rip_relative;

    constexpr BIndirectSig(PD pattern, ptrdiff_t offset, uint8_t length, bool signed_, bool rip_relative) :
        pattern(std::move(pattern)), offset(offset), length(length), signed_(signed_), rip_relative(rip_relative)
    {}

    const uint8_t *find_unique_match_or_none(const uint8_t *seq_start, size_t seq_size_bytes, const SeqToVaCb &seq_to_va) const override {
        const uint8_t *addr = this->pattern.find_unique_match_or_none(seq_start, seq_size_bytes);
        if(addr == nullptr) {
            return nullptr;
        } else {
            int64_t operand_ext;
            if(this->signed_) {
                // Read with sign extension
                switch(this->length) {
                    case 1: { int8_t tmp; std::memcpy(&tmp, addr + this->offset, 1); operand_ext = tmp; break; }
                    case 2: { int16_t tmp; std::memcpy(&tmp, addr + this->offset, 2); operand_ext = tmp; break; }
                    case 4: { int32_t tmp; std::memcpy(&tmp, addr + this->offset, 4); operand_ext = tmp; break; }
                    case 8: { std::memcpy(&operand_ext, addr + this->offset, 8); break; }
                    default:
                        return nullptr;
                }
            } else {
                // Read as unsigned
                switch(this->length) {
                    case 1: { uint8_t tmp; std::memcpy(&tmp, addr + this->offset, 1); operand_ext = tmp; break; }
                    case 2: { uint16_t tmp; std::memcpy(&tmp, addr + this->offset, 2); operand_ext = tmp; break; }
                    case 4: { uint32_t tmp; std::memcpy(&tmp, addr + this->offset, 4); operand_ext = tmp; break; }
                    case 8: { std::memcpy(&operand_ext, addr + this->offset, 8); break; }
                    default:
                        return nullptr;
                }
            }

            if (rip_relative) {
                const uint8_t *addr_va = seq_to_va(addr);
                if(addr_va == nullptr) {
                    return nullptr;
                }
                const uint8_t *rip = addr_va + this->offset + length;
                return rip + operand_ext;
            } else {
                return reinterpret_cast<uint8_t *>(operand_ext);
            }
        }
    }
};

class IndirectSig {
public:
    IndirectSig() = delete;

    template<FixedString FS>
    static consteval auto make(ptrdiff_t offset, uint8_t length, bool signed_, bool rip_relative) {
        auto pd = PatternDescriptor::make<FS>();
        return BIndirectSig(pd, offset, length, signed_, rip_relative);
    }

    static BIndirectSig<VectorPatternDescriptor> make(const std::string_view sv, ptrdiff_t offset, uint8_t length, bool signed_, bool rip_relative) {
        VectorPatternDescriptor pd = PatternDescriptor::make(sv);
        return BIndirectSig(pd, offset, length, signed_, rip_relative);
    }
};

#undef USE_SSE2_INTRINSICS_STAGE1
#undef USE_AVX2_INTRINSICS_STAGE1
#undef USE_SSE2_INTRINSICS_STAGE2
#undef USE_AVX2_INTRINSICS_STAGE2
#undef MUST_INLINE
#undef TARGET
