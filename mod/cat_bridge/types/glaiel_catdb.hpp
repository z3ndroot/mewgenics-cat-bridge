#pragma once

#include "types/glaiel_ecs.hpp"

#include <cstddef>
#include <cstdint>

// Reconstructions of Mewgenics structures.
//
// CatDatabase: a component in the "Shared" scene holding cat-wide records.
// Partial layout, reverse-engineered for cat_bridge (2026-09-24, v1.1.21239)
// by locating the save file's `pedigree` records in live memory.
//
// The maps are Swiss-table style open-addressing hash maps (same layout the
// save's `pedigree` blob serializes): one control byte per slot, < 0x80 =
// occupied; slots are a flat array.

template<typename Slot>
struct SwissTable {
    const int8_t *ctrl;
    Slot *slots;
    uint64_t size;
    uint64_t capacity; // 2^n - 1
    char _20[0x10];
    uint64_t growth_left;

    template<typename F>
    void for_each(F &&fn) const {
        if(ctrl == nullptr || slots == nullptr) {
            return;
        }
        for(uint64_t i = 0; i < capacity; i++) {
            if(ctrl[i] >= 0) {
                fn(slots[i]);
            }
        }
    }
};
static_assert(sizeof(SwissTable<void *>) == 0x38);

struct PedigreeEntry {
    int64_t sql_key;
    int64_t parent_a; // -1 if unknown (e.g. strays)
    int64_t parent_b;
    double coi;       // coefficient of inbreeding = kinship(parent_a, parent_b)
};
static_assert(sizeof(PedigreeEntry) == 32);

struct KinshipEntry {
    int64_t a;
    int64_t b;
    double kinship;   // memoized coefficient of kinship; kinship(x, x) = (1 + coi) / 2
};
static_assert(sizeof(KinshipEntry) == 24);

struct CatDatabase : Component {
    SwissTable<PedigreeEntry> pedigree;   // every cat ever, keyed by sql_key
    SwissTable<KinshipEntry> kinship_cache;
    SwissTable<int64_t> unknown_key_set;  // 24 sql_keys in the test save; meaning unknown
};
static_assert(offsetof(CatDatabase, pedigree) == 0x38);
static_assert(offsetof(CatDatabase, kinship_cache) == 0x70);
static_assert(offsetof(CatDatabase, unknown_key_set) == 0xa8);
