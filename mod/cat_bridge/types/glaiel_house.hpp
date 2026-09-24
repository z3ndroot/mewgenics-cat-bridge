#pragma once

#include "types/glaiel_ecs.hpp"
#include "types/msvc.hpp"

#include <cstddef>
#include <cstdint>

// Reconstructions of Mewgenics structures.
//
// The house. Partial layouts, reverse-engineered for cat_bridge (2026-09-24,
// v1.1.21239) by diffing live objects against the save's house_state roster
// and the in-game room/furniture UI.

// One summed room effect, e.g. {"Comfort", 8.0} or {"Stimulation", 13.0}.
// Names match the keys in data/furniture_effects.gon. Only non-zero
// effects are present.
struct RoomEffect {
    MsvcReleaseModeXString name;
    double value;
};
static_assert(sizeof(RoomEffect) == 40);

// A room. Its component type is "FurnitureGrid".
struct HouseRoom : Component {
    char _38[8];
    MsvcReleaseModeXString name; // "Floor1_Small", "Floor1_Large", "Attic", ...
    char _60[0x140 - 0x60];
    MsvcReleaseModeVector<RoomEffect> effects; // totals from the furniture in the room
};
static_assert(offsetof(HouseRoom, name) == 0x40);
static_assert(offsetof(HouseRoom, effects) == 0x140);

struct HouseCat : Component {
    char _38[0xe8 - 0x38];
    // Room the cat lives in; nullptr for the daily stray waiting outside.
    HouseRoom *room;
};
static_assert(offsetof(HouseCat, room) == 0xe8);

// Placed furniture's persistent record (same data the save stores).
struct FurnitureInstance {
    int64_t id;
    MsvcReleaseModeXString type; // key in data/furniture_effects.gon, e.g. "object_box"
    char _28[8];
    MsvcReleaseModeXString room;
    int32_t x;
    int32_t y;
};
static_assert(offsetof(FurnitureInstance, room) == 0x30);
static_assert(offsetof(FurnitureInstance, x) == 0x50);

// Component type "FurniturePiece": one per placed piece of furniture.
struct FurniturePiece : Component {
    char _38[0x10];
    HouseRoom *room;
    char _50[0x2d8 - 0x50];
    FurnitureInstance *instance;
};
static_assert(offsetof(FurniturePiece, room) == 0x48);
static_assert(offsetof(FurniturePiece, instance) == 0x2d8);

// A parsed GON node (Tyler Glaiel's data format, https://github.com/TylerGlaiel/GON)
// as laid out in Mewgenics 1.1.21239. Reverse-engineered 2026-09-25 from the
// live furniture_effects tree: children by index at +0x38, the number both
// as int (+0x50) and double (+0x58), the raw text (+0x68), the key (+0x88)
// and the type (+0xa8: 1 string, 2 number, 3 object -- matches GON's enum).
struct GonObject {
    char _0[0x38];                             // name -> index map (not used)
    MsvcReleaseModeVector<GonObject> children;
    int32_t int_data;
    char _54[4];
    double float_data;
    char _60[8];                               // bool_data + padding
    MsvcReleaseModeXString string_data;
    MsvcReleaseModeXString name;
    int32_t type;
    char _ac[4];
};
static_assert(sizeof(GonObject) == 0xb0);
static_assert(offsetof(GonObject, children) == 0x38);
static_assert(offsetof(GonObject, float_data) == 0x58);
static_assert(offsetof(GonObject, string_data) == 0x68);
static_assert(offsetof(GonObject, name) == 0x88);
static_assert(offsetof(GonObject, type) == 0xa8);

// data/furniture_effects.gon, loaded once into the "SpawnDatabase" component
// (its loader, ~RVA 0x7a79cb in Mewgenics.exe, copies the file's GON tree to
// this + 0xd48). The room effect totals are recomputed from it all the time.
inline constexpr size_t SPAWNDATABASE_FURNITURE_EFFECTS = 0xd48;
inline constexpr int32_t GON_NUMBER = 2;
