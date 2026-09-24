#pragma once

#include "types/glaiel_cat.hpp"
#include "types/glaiel_ecs.hpp"
#include "types/msvc.hpp"

// Reconstructions of Mewgenics structures.
//
// Miscellaneous game world things, like
// inventory and scene controllers.
//
// polymeric 2026

struct InventoryKVPair {
    int64_t key;
    Equipment val;
};
struct Inventory : Component {
    MsvcReleaseModeXHash<InventoryKVPair> backpack;
    MsvcReleaseModeXHash<InventoryKVPair> storage;
    MsvcReleaseModeXHash<InventoryKVPair> trash;
    char _f8[8];
    char _100[8];
    char _108[0x30];
    char _138[4];
    char _13c[4];
    char _140[4];
    char _144[4];
    char _148[4];
    char _14c[4];
    char _150[8];
};
