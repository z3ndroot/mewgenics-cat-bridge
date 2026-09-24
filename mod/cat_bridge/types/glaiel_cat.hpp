#pragma once

#include "types/glaiel_container.hpp"
#include "types/glaiel_ecs.hpp"
#include "types/msvc.hpp"

#include <cstdint>

// Reconstructions of Mewgenics structures.
//
// Cats.
//
// polymeric 2026

// Are cats a fundamental library component?
// 8/10 computer scientists say "yes".
//
// 1 dissenter was executed.
//
// Are cats a fundamental library component?
// 9/9 computer scientists say "yes".

struct BodyPartDescriptor {
    uint32_t field_0;
    uint32_t part_sprite_idx;
    uint32_t texture_sprite_idx;
    uint32_t scar_sprite_idx;
    uint32_t unknown_1;
    uint32_t unknown_2;
    char _18[0x28];
    char _40[0x14];
};
// assure that we don't break BodyData field alignment
static_assert(sizeof(BodyPartDescriptor) == 0x54);

struct BodyParts {
    char _0[8];
    char _8[8];
    char _10[8];
    uint32_t texture_sprite_idx;
    uint32_t heritable_palette_idx;
    uint32_t collar_palette_idx;
    uint32_t unknown_1;
    uint32_t unknown_0;
    BodyPartDescriptor body;
    BodyPartDescriptor head;
    BodyPartDescriptor tail;
    BodyPartDescriptor leg1;
    BodyPartDescriptor leg2;
    BodyPartDescriptor arm1;
    BodyPartDescriptor arm2;
    BodyPartDescriptor lefteye;
    BodyPartDescriptor righteye;
    BodyPartDescriptor lefteyebrow;
    BodyPartDescriptor righteyebrow;
    BodyPartDescriptor leftear;
    BodyPartDescriptor rightear;
    BodyPartDescriptor mouth;
    int32_t field_4c4;
    char _4c8[4];
    char _4cc[8];
    char _4d4[8];
    char _4dc[4];
    char _4e0[4];
    char _4e4[8];
    char _4ec[8];
    char _4f4[4];
    char _4f8[4];
    char _4fc[4];
    char _500[4];
    char _504[8];
    char _50c[8];
    char _514[4];
    int32_t field_518;
    char _51c[4];
    char _520[8];
    char _528[8];
    char _530[4];
    char _534[4];
    char _538[8];
    char _540[8];
    char _548[4];
    char _54c[4];
    char _550[4];
    char _554[4];
    char _558[8];
    char _560[8];
    char _568[4];
    int32_t field_56c;
    char _570[4];
    char _574[8];
    char _57c[4];
    char _580[4];
    char _584[4];
    char _588[4];
    char _58c[8];
    char _594[8];
    char _59c[4];
    char _5a0[4];
    char _5a4[4];
    char _5a8[4];
    char _5ac[8];
    char _5b4[8];
    char _5bc[4];
    int32_t field_5c0;
    char _5c4[4];
    char _5c8[8];
    char _5d0[8];
    char _5d8[4];
    char _5dc[4];
    char _5e0[8];
    char _5e8[8];
    char _5f0[4];
    char _5f4[4];
    char _5f8[4];
    char _5fc[4];
    char _600[8];
    char _608[8];
    char _610[4];
    int32_t field_614;
    char _618[4];
    char _61c[8];
    char _624[8];
    char _62c[4];
    char _630[4];
    char _634[8];
    char _63c[4];
    char _640[4];
    char _644[4];
    char _648[4];
    char _64c[4];
    char _650[4];
    char _654[8];
    char _65c[8];
    char _664[4];
    MsvcReleaseModeXString voice;
    double pitch;
};
// assure that we don't break CatData field alignment
static_assert(sizeof(BodyParts) == 0x690);

struct CatStats {
    int32_t str;
    int32_t dex;
    int32_t con;
    int32_t int_;
    int32_t spd;
    int32_t cha;
    int32_t lck;
};
// assure that we don't break CatData field alignment
static_assert(sizeof(CatStats) == 0x1c);

struct StatModifier {
    /* GonObject */char expression[179];
    int32_t battles_remaining;
};
// assure that we don't break CampaignStats field alignment
static_assert(sizeof(StatModifier) == 0xB8);

struct CampaignStats {
    int32_t hp;
    bool dead;
    bool unknown_0;
    char _6[2];
    uint32_t unknown_1;
    char _c[4];
    MsvcReleaseModeVector<StatModifier> event_stat_modifiers;
};
// assure that we don't break CatData field alignment
static_assert(sizeof(CampaignStats) == 0x28);

struct Equipment {
    int64_t id; // not serialized
    MsvcReleaseModeXString name;
    MsvcReleaseModeXString aux_string;
    int32_t uses_left;
    int32_t unknown_2;
    int32_t unknown_3;
    int32_t unknown_4;
    uint8_t unknown_5;
    char _59[3];
    uint8_t times_taken_on_adventure;
    char _5d[3];
};
// assure that we don't break CatData field alignment
static_assert(sizeof(Equipment) == 0x60);

struct CatData {
    uint64_t entropy;
    podvector<uint8_t> house_boss_kills;
    MsvcReleaseModeXWString name;
    MsvcReleaseModeXString nameplate_symbol;
    int32_t sex;
    int32_t sex_dup;
    BodyParts body_parts;
    CatStats stats_heritable;
    CatStats stats_delta_levelling;
    CatStats stats_delta_injuries;
    int32_t injuries[0x10];
    char _784[4];
    MsvcReleaseModeXString last_injury_debuffed_stat;
    CampaignStats campaign_stats;
    MsvcReleaseModeXString actives_basic[2];
    MsvcReleaseModeXString actives_accessible[4];
    MsvcReleaseModeXString actives_inherited[4];
    MsvcReleaseModeXString passive_0;
    int64_t passive_0_level;
    MsvcReleaseModeXString passive_1;
    int64_t passive_1_level;
    MsvcReleaseModeXString mutation_0;
    int64_t mutation_0_level;
    MsvcReleaseModeXString mutation_1;
    int64_t mutation_1_level;
    Equipment head;
    Equipment face;
    Equipment neck;
    Equipment weapon;
    Equipment trinket;
    MsvcReleaseModeXString unknown_2;
    int32_t unknown_3;
    char _bb4[4];
    double libido;
    double sexuality;
    int64_t lover_sql_key;
    double lover_affinity;
    int64_t hater_sql_key;
    double hater_affinity;
    double aggression;
    double fertility;
    uint64_t flags;
    uint64_t cleared_zones;
    uint8_t completed_act;
    uint8_t completed_chapter;
    uint8_t completed_difficulty;
    char _c0b[5];
    MsvcReleaseModeXString collar;
    uint32_t level;
    uint32_t lifestage;
    int64_t birthday;
    int64_t deathday_house;
    int64_t sql_key;
    double coi;
};
// this is a golden value
// from new/memset around ctor call site
static_assert(sizeof(CatData) == 0xc58);

struct CatParts : Component {
    char _38[8];
    char _40[8];
    char _48[8];
    char _50[8];
    char _58[0xc];
    bool permanent_kitten;
    char _65[7];
    char _6c[0x14];
    char _80[0x20];
    char _a0[8];
    char _a8[8];
    char _b0[8];
    char _b8[8];
    char _c0[8];
    char _c8[0x10];
    char _d8[0x10];
    char _e8[0x10];
    char _f8[8];
    char _100[8];
    char _108[0x10];
    char _118[8];
    char _120[0x20];
    char _140[0x20];
    char _160[8];
    char _168[4];
    char _16c[4];
    char _170[8];
    char _178[2];
    char _17a[6];
    char _180[0x18];
    char _198[0x1c];
    char _1b4[4];
    char _1b8[8];
    char _1c0[0x40];
    char _200[0x40];
    char _240[0x40];
    char _280[0x40];
    char _2c0[0x40];
    char _300[0x40];
    char _340[0x40];
    char _380[0x40];
    char _3c0[0x40];
    char _400[0x40];
    char _440[0x40];
    char _480[0x40];
    char _4c0[0x40];
    char _500[0x40];
    char _540[0x40];
    char _580[0x40];
    char _5c0[0x40];
    char _600[0x40];
    char _640[0x20];
    int32_t head;
    char _664[0x1c];
    char _680[0x34];
    int32_t neck;
    char _6b8[8];
    char _6c0[0x40];
    char _700[8];
    int32_t face;
    char _70c[0x34];
    char _740[0x1c];
    int32_t weapon;
    char _760[0x20];
    char _780[0x30];
    int32_t trinket;
    char _7b4[0xc];
    char _7c0[0x40];
    char _800[0x28];
    char _828[8];
    char _830[0x10];
    char _840[0x18];
    char _858[0x28];
    char _880[0x28];
    CatData* cat;
    char _8b0[0x10];
    char _8c0[8];
    char _8c8[0x18];
    char _8e0[0x18];
    char _8f8[8];
    char _900[0x10];
    char _910[0x18];
    char _928[0x18];
    char _940[0x18];
    char _958[0x18];
    char _970[0x10];
    char _980[8];
    char _988[0x18];
    char _9a0[0x18];
    char _9b8[8];
    char _9c0[0x10];
    char _9d0[0x18];
    char _9e8[0x18];
    char _a00[0x18];
    char _a18[0x18];
    char _a30[0x10];
    char _a40[8];
    char _a48[0x18];
    char _a60[0x18];
    char _a78[8];
    char _a80[0x10];
};
// golden value from new/memset
static_assert(sizeof(CatParts) == 0xa90);
