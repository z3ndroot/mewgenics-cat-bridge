#pragma once

#include "utilities/constexpr.hpp"

#include <climits>
#include <fstream>
#include <array>
#include <optional>
#include <filesystem>
#include <span>
#include <stdexcept>

#include "tomcrypt.h"

// Checksum and integrity check utilities
//
// polymeric 2026

typedef std::array<uint8_t, 32> Hash256Bit;
inline std::optional<Hash256Bit> sha256_file(const std::filesystem::path &path) {
    std::ifstream exe_file(path, std::ios::binary);
    if(!exe_file) {
        return std::nullopt;
    }

    hash_state md;
    sha256_init(&md);

    std::array<char, 4096> read_buffer;
    while(exe_file.read(read_buffer.data(), read_buffer.size()) || exe_file.gcount() > 0) {
        sha256_process(&md, reinterpret_cast<uint8_t *>(read_buffer.data()), static_cast<unsigned long>(exe_file.gcount()));
    }

    if(exe_file.bad()) {
        return std::nullopt;
    }

    Hash256Bit digest;
    sha256_done(&md, digest.data());

    return digest;
}

inline Hash256Bit sha256_span(const std::span<const uint8_t> span) {
    hash_state md;
    sha256_init(&md);

    size_t offset = 0;
    while(span.size() - offset >= ULONG_MAX) {
        sha256_process(&md, span.data() + offset, ULONG_MAX);
        offset += ULONG_MAX;
    }

    if(offset < span.size()) {
        sha256_process(&md, span.data() + offset, static_cast<unsigned long>(span.size() - offset));
    }

    Hash256Bit digest;
    sha256_done(&md, digest.data());

    return digest;
}

inline constexpr Hash256Bit c_str_to_hash256bit(const char (&str)[65]) {
    Hash256Bit digest;
    for (int i = 0; i < 32; i++) {
        int stroff = i * 2;
        uint8_t high_nibble = parse_char_0_to_F_as_hex(str[stroff]);
        uint8_t low_nibble = parse_char_0_to_F_as_hex(str[stroff + 1]);
        if(high_nibble >= 16 || low_nibble >= 16) {
            throw std::logic_error("Given hex sequence has unexpected characters");
        }
        digest[i] = (high_nibble << 4) | low_nibble;
    }
    return digest;
}

inline std::string hash256bit_to_string(const Hash256Bit &digest) {
    std::string str;
    for (int i = 0; i < 32; i++) {
        str += std::format("{:02x}", digest[i]);
    }
    return str;
}
