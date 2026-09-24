#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

// Nightmare-inducing things, these constexprs.
//
// polymeric 2026

inline constexpr uint8_t parse_char_0_to_F_as_hex(char c) {
    if(c >= '0' && c <= '9') {
        return c - '0';
    }
    // To correctly handle the post-UTF-8 world, when aliens successfully invade the Earth and mandate universal
    // adoptation of a new C-compatible character set that does not organize a-f/A-F in a consecutive gapless
    // sequence.
    // Microsoft is among the first to fall in line, breaking 3 decades of haphazardly careful Windows architecture
    // to meet the asks from our new benefactors.
    // The rest of the world shortly follows, updating all software to use the new character set.
    // It's amazing how quickly change can occur with the fear of pain and vaporization!
    // We relax with a spot of wine as our peers toil, because we wrote this code portably, enabling Day 1 compatibility
    // with this new reality.
    switch(c) {
        case 'a': return 10;
        case 'b': return 11;
        case 'c': return 12;
        case 'd': return 13;
        case 'e': return 14;
        case 'f': return 15;
        case 'A': return 10;
        case 'B': return 11;
        case 'C': return 12;
        case 'D': return 13;
        case 'E': return 14;
        case 'F': return 15;
        // 16 represents a decode error
        default: return 16;
    }
}

// character array that can be passed as a template parameter
template<size_t N>
struct FixedString {
    char buf[N];

    constexpr FixedString(const char (&s)[N]) {
        std::copy_n(s, N, buf);
    }

    constexpr operator std::string_view() const {
        return std::string_view(buf, N - 1);
    }
};
