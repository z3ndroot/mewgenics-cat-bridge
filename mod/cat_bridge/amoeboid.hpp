#pragma once

#include "utilities/checksum.hpp"
#include "utilities/signature.hpp"

#include <cstdint>
#include <optional>

// Main program declarations.
//
// Adapted from the "amoeboid" base DLL framework used by
// https://github.com/p0lymeric/mewgenics_randomize_item_picks (MIT licensed).
// Original signatures/offsets by polymeric, 2026.

// CONSTANTS

// Mod information

inline constexpr char MOD_AUTHOR[] = "z3ndroot";
inline constexpr char MOD_NAME[] = "Cat Bridge (MCP)";
inline constexpr char MOD_IDENTIFIER[] = "z3ndroot.cat_bridge_mcp";
inline constexpr char MOD_URL[] = "https://github.com/z3ndroot/mewgenics-cat-bridge";
inline constexpr char MOD_VERSION[] = "0.1.0";

// These addresses were extracted from Mewgenics.exe by the upstream
// randomize_item_picks project. They are reused here because we need the
// exact same two symbols: the MewDirector singleton pointer, and its
// always_update function (used as a per-frame pump for our pipe server).
//
// *** IMPORTANT ***
// These offsets/hash are only valid for the specific Mewgenics.exe build
// that the upstream project targeted (EXE_VERSION below). Mewgenics updates
// frequently, and this DLL will REFUSE TO LOAD (harmlessly) if your copy's
// SHA-256 doesn't match.
//
// To fix that after a game update:
//   1. Edit misc/find_rvas.py: set MEWGENICS_EXE_PATH to your real Mewgenics.exe path.
//   2. pip install -r misc/requirements.txt
//   3. python misc/find_rvas.py
//   4. Paste the two ADDRESS_/DATAOFF_ lines and the new EXE_SHA256 it prints
//      below, replacing the ones here. Bump EXE_VERSION to match the new
//      game version (Mewgenics.exe > Properties, or the Steam build notes).
//
// If find_rvas.py reports <NOT FOUND> or <MULTIPLE MATCHES> for a signature,
// the function's bytes changed and the hex pattern itself needs to be
// re-derived from a fresh Ghidra/IDA disassembly of the new exe. That's a
// bigger job -- see docs/DEVELOPMENT.md.

// Semantic release version of the Mewgenics.exe binary last used to update hardcoded offsets
inline constexpr char EXE_VERSION[] = "1.1.21239"; // verified 2026-09-24 against local Steam copy

// SHA-256 hash of the Mewgenics.exe binary last used to update hardcoded offsets
inline constexpr Hash256Bit EXE_SHA256 = c_str_to_hash256bit("4127cd6a792ae528bca6f65a8873dd61789591937d87656c2b586a5e30eb77ea"); // verified 2026-09-24

// Function offsets are encoded as relative VAs.
// Hooked so we can pump the pipe server once per game update-frame.
inline constexpr const auto ADDRESS_glaiel__MewDirector__always_update = DirectSig::make<"48 8B 05 ?? ?? ?? ?? F2 0F 10 05 ?? ?? ?? ?? 48 FF 81 30 05 00 00 F2 0F 5E 80 C8 0D 00 00 F2 0F 58 81 38 05 00 00">(0);

// Data offsets are encoded as relative VAs.
// This is the root: MewDirector singleton -> Director -> scenes -> components -> CatParts -> CatData.
inline constexpr const auto DATAOFF_glaiel__MewDirector__p_singleton = IndirectSig::make<"48 89 5C 24 10 48 89 4C 24 08 57 48 83 EC 40 48 8B CA 48 8B 05 ?? ?? ?? ?? 48 8B B8 A8 05 00 00">(21, 4, true, true);

// CROSS-TU DECLARATIONS

// The "everything" struct
// Exporter: amoeboid.cpp
struct GlobalContext;
extern GlobalContext G;

// TYPE DECLARATIONS

struct GlobalContext {
    // cat_bridge.dll offset.
    uintptr_t dll_base_va;
    uintptr_t dll_image_size;

    // Mewgenics.exe offset.
    uintptr_t host_exec_base_va;
    uintptr_t host_exec_image_size;

    // Whether it is permissible for the dll to self-eject.
    bool dll_can_self_eject;

    // Mewgenics.exe hash.
    std::optional<Hash256Bit> exe_actual_sha256;
    bool exe_hash_mismatch_detected;
};
