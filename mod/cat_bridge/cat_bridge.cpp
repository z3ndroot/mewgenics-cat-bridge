#include "amoeboid.hpp"
#include "json_writer.hpp"
#include "pipe_server.hpp"
#include "types/glaiel.hpp"
#include "types/msvc.hpp"
#include "utilities/debug_console.hpp"
#include "utilities/function_hook.hpp"
#include "utilities/portal.hpp"
#include "utilities/strings.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <charconv>
#include <map>

// cat_bridge: dumps/edits live cat data over a named pipe, for an external
// MCP server to expose to an LLM.
//
// Architecture recap (see pipe_server.hpp for the "why"):
//   PipeServer background thread <--queues--> this file's on_update_frame(),
//   which runs once per game frame via the always_update hook (same hook
//   point the upstream randomize_item_picks mod uses).
//
// Commands (see handle_request): reads for cats, pedigree and rooms; writes
// for base stats, HP, body parts and passives; debug dumps used to
// reverse-engineer new structures. Extending it mostly means adding more
// serialize_*/command code using the struct layouts in types/glaiel_*.hpp
// -- see docs/DEVELOPMENT.md for what's verified and what's still open.

namespace {

struct PrivateState {
    PipeServer pipe{"cat_bridge"};
};

PrivateState P;

} // namespace

MAKE_SDPORTAL(DATAOFF_glaiel__MewDirector__p_singleton,
    MewDirector *, get_p_mewdirector_singleton
)

// The in-game day counter (save property "current_day"): an int64 at
// MewDirector + 0x580. Found in Mewgenics.exe 1.1.21239: the save loader
// (~RVA 0x3a6dda) stores current_day there, and the breeding code reads the
// same global (RVA 0x13dac30 = the MewDirector singleton). -1 if unloaded.
int64_t current_day() {
    MewDirector *p_mewdirector = get_p_mewdirector_singleton();
    if(p_mewdirector == nullptr) {
        return -1;
    }
    return *reinterpret_cast<const int64_t *>(reinterpret_cast<const uint8_t *>(p_mewdirector) + 0x580);
}

// House food and gold: two int32 at HouseInventory + 0xb0 / + 0xb4 (save
// properties "house_food" / "house_gold"). Found in Mewgenics.exe 1.1.21239:
// the house loader (~RVA 0x20680a) reads "house_food" (default 25) into
// [obj + 0xb0] and the next property into [obj + 0xb4]; the saver writes them
// back from there. Live values matched the save (gold 33) and food dropped by
// one per house cat overnight (97 -> 78 with 19 cats).
int32_t *house_money();

std::string get_type_name(Component *component) {
    MsvcReleaseModeXString type_name = {};
    component->vtable->GetObjectTypeSTR(component, &type_name);
    std::string result(type_name.as_native_string_view());
    type_name.destroy();
    return result;
}

// Cats in the house scene have a HouseCat component on the same entity as
// their CatParts. Other CatParts are UI (e.g. CatStatsDrawer panels, which
// show a cat plus its parents, dead or alive) and share the same CatData.
// The daily stray waiting outside also has a HouseCat, but no room.
HouseCat *find_house_cat(CatParts *parts) {
    if(parts->entity == nullptr) {
        return nullptr;
    }
    for(auto p_sibling : parts->entity->components) {
        if(p_sibling != nullptr && get_type_name(p_sibling) == "HouseCat") {
            return static_cast<HouseCat *>(p_sibling);
        }
    }
    return nullptr;
}

// Walk every loaded Scene's ComponentLists looking for "CatParts" components,
// and pull out the CatData* each one points to. Keyed by sql_key so cats
// that are reachable from several CatParts aren't duplicated.
struct FoundCat {
    CatData *cat;
    HouseCat *house_cat; // nullptr if only seen in UI
};

template<typename F>
void for_each_cat_parts(F &&fn) {
    MewDirector *p_mewdirector = get_p_mewdirector_singleton();
    if(p_mewdirector == nullptr || p_mewdirector->director == nullptr) {
        return;
    }

    for(auto p_scene : p_mewdirector->director->scenes) {
        if(p_scene == nullptr || p_scene->ComponentLists == nullptr) {
            continue;
        }
        for(auto p_component : *p_scene->ComponentLists) {
            if(get_type_name(p_component) == "CatParts") {
                fn(p_scene, static_cast<CatParts *>(p_component));
            }
        }
    }
}

Component *find_component(std::string_view type_name) {
    MewDirector *p_mewdirector = get_p_mewdirector_singleton();
    if(p_mewdirector == nullptr || p_mewdirector->director == nullptr) {
        return nullptr;
    }
    for(auto p_scene : p_mewdirector->director->scenes) {
        if(p_scene == nullptr || p_scene->ComponentLists == nullptr) {
            continue;
        }
        for(auto p_component : *p_scene->ComponentLists) {
            if(get_type_name(p_component) == type_name) {
                return p_component;
            }
        }
    }
    return nullptr;
}

int32_t *house_money() {
    Component *inv = find_component("HouseInventory");
    if(inv == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<int32_t *>(reinterpret_cast<uint8_t *>(inv) + 0xb0);
}

std::unordered_map<int64_t, FoundCat> collect_all_cats() {
    std::unordered_map<int64_t, FoundCat> cats;
    for_each_cat_parts([&](Scene *, CatParts *parts) {
        if(parts->cat != nullptr) {
            auto [it, _] = cats.try_emplace(parts->cat->sql_key, FoundCat{parts->cat, nullptr});
            if(it->second.house_cat == nullptr) {
                it->second.house_cat = find_house_cat(parts);
            }
        }
    });
    return cats;
}

void serialize_stats(JsonWriter &w, std::string_view key, const CatStats &s) {
    w.key(key);
    w.begin_object();
    w.kv("str", s.str).kv("dex", s.dex).kv("con", s.con).kv("int", s.int_);
    w.kv("spd", s.spd).kv("cha", s.cha).kv("lck", s.lck);
    w.end_object();
}

void serialize_cat(JsonWriter &w, const FoundCat &found) {
    CatData *cat = found.cat;
    w.begin_object();
    w.kv("sql_key", cat->sql_key);
    HouseRoom *room = found.house_cat != nullptr ? found.house_cat->room : nullptr;
    w.kv("in_house", room != nullptr);
    w.kv("outside", found.house_cat != nullptr && room == nullptr); // the daily stray
    w.kv("room", room != nullptr ? room->name.as_native_string_view() : std::string_view());
    w.kv("name", convert_utf16_wstring_to_utf8_string(cat->name.as_native_wstring_view()));
    w.kv("level", cat->level);
    w.kv("lifestage", cat->lifestage);
    w.kv("hp", cat->campaign_stats.hp);
    w.kv("dead", cat->campaign_stats.dead);
    w.kv("collar", cat->collar.as_native_string_view());

    // Breeding-relevant fields. Semantics of sex values / libido etc. are
    // taken at face value from the reconstructed field names.
    w.kv("sex", cat->sex);
    w.kv("coi", cat->coi);
    w.kv("birthday", cat->birthday);
    w.kv("libido", cat->libido);
    w.kv("sexuality", cat->sexuality);
    w.kv("fertility", cat->fertility);
    w.kv("aggression", cat->aggression);
    w.kv("lover_sql_key", cat->lover_sql_key);
    w.kv("lover_affinity", cat->lover_affinity);
    w.kv("hater_sql_key", cat->hater_sql_key);
    w.kv("hater_affinity", cat->hater_affinity);

    // Raw stat components. The in-game total also adds the collar's class
    // stat_mods (and item/mutation bonuses) from the game's .gon data; the
    // MCP server computes that, since the data lives in resources.gpak.
    serialize_stats(w, "stats_base", cat->stats_heritable);
    serialize_stats(w, "stats_levelling", cat->stats_delta_levelling);
    serialize_stats(w, "stats_injuries", cat->stats_delta_injuries);

    // mutation_* hold disorders (Pox, AcidReflux, ...); both kinds are
    // defined in data/passives/*.gon and can carry per-level stat bonuses.
    w.key("passives");
    w.begin_array();
    struct PassiveSlot { const char *slot; const MsvcReleaseModeXString *name; int64_t level; };
    for(auto [slot, name, level] : {PassiveSlot{"passive1", &cat->passive_0, cat->passive_0_level},
                                    PassiveSlot{"passive2", &cat->passive_1, cat->passive_1_level},
                                    PassiveSlot{"disorder1", &cat->mutation_0, cat->mutation_0_level},
                                    PassiveSlot{"disorder2", &cat->mutation_1, cat->mutation_1_level}}) {
        auto sv = name->as_native_string_view();
        if(sv.empty() || sv == "None") {
            continue;
        }
        w.begin_object().kv("slot", std::string_view(slot)).kv("name", sv).kv("level", level).end_object();
    }
    w.end_array();

    // Active abilities by internal key (data/abilities/*.gon); "None" = empty
    // slot, skipped. basic = move + basic attack; accessible = usable in
    // battle; inherited = what the cat was born with (see docs/DEVELOPMENT.md).
    w.key("actives");
    w.begin_object();
    auto write_list = [&](std::string_view key, const auto &list) {
        w.key(key);
        w.begin_array();
        for(const auto &s : list) {
            auto sv = s.as_native_string_view();
            if(!sv.empty() && sv != "None") {
                w.value(sv);
            }
        }
        w.end_array();
    };
    write_list("basic", cat->actives_basic);
    write_list("accessible", cat->actives_accessible);
    write_list("inherited", cat->actives_inherited);
    w.end_object();

    // Sprite index per body part. Indices >= 300 are mutations and -2 a
    // missing part, both defined (with stat bonuses) in data/mutations/*.gon.
    w.key("body_parts");
    w.begin_object();
    const BodyParts &bp = cat->body_parts;
    w.kv("texture", static_cast<int32_t>(bp.texture_sprite_idx));
    for(auto [part_name, part] : {std::pair{"body", &bp.body}, {"head", &bp.head}, {"tail", &bp.tail},
                                  {"leg1", &bp.leg1}, {"leg2", &bp.leg2}, {"arm1", &bp.arm1}, {"arm2", &bp.arm2},
                                  {"lefteye", &bp.lefteye}, {"righteye", &bp.righteye},
                                  {"lefteyebrow", &bp.lefteyebrow}, {"righteyebrow", &bp.righteyebrow},
                                  {"leftear", &bp.leftear}, {"rightear", &bp.rightear}, {"mouth", &bp.mouth}}) {
        w.kv(part_name, static_cast<int32_t>(part->part_sprite_idx));
    }
    w.end_object();

    w.key("equipment");
    w.begin_object();
    w.kv("head", cat->head.name.as_native_string_view());
    w.kv("face", cat->face.name.as_native_string_view());
    w.kv("neck", cat->neck.name.as_native_string_view());
    w.kv("weapon", cat->weapon.name.as_native_string_view());
    w.kv("trinket", cat->trinket.name.as_native_string_view());
    w.end_object();

    w.end_object();
}

// Debug view: every stat-relevant field we know about, un-summed, to work
// out how the game computes the stats it displays.
void serialize_cat_debug(JsonWriter &w, CatData *cat) {
    w.begin_object();
    w.kv("sql_key", cat->sql_key);
    w.kv("name", convert_utf16_wstring_to_utf8_string(cat->name.as_native_wstring_view()));
    w.kv("collar", cat->collar.as_native_string_view());
    serialize_stats(w, "stats_heritable", cat->stats_heritable);
    serialize_stats(w, "stats_delta_levelling", cat->stats_delta_levelling);
    serialize_stats(w, "stats_delta_injuries", cat->stats_delta_injuries);
    w.kv("last_injury_debuffed_stat", cat->last_injury_debuffed_stat.as_native_string_view());
    w.kv("event_stat_modifiers_count", static_cast<uint64_t>(cat->campaign_stats.event_stat_modifiers.size()));
    w.kv("passive_0", cat->passive_0.as_native_string_view()).kv("passive_0_level", cat->passive_0_level);
    w.kv("passive_1", cat->passive_1.as_native_string_view()).kv("passive_1_level", cat->passive_1_level);
    w.kv("mutation_0", cat->mutation_0.as_native_string_view()).kv("mutation_0_level", cat->mutation_0_level);
    w.kv("mutation_1", cat->mutation_1.as_native_string_view()).kv("mutation_1_level", cat->mutation_1_level);

    w.key("body_parts");
    w.begin_object();
    const BodyParts &bp = cat->body_parts;
    w.kv("texture_sprite_idx", bp.texture_sprite_idx);
    for(auto [part_name, part] : {std::pair{"body", &bp.body}, {"head", &bp.head}, {"tail", &bp.tail},
                                  {"leg1", &bp.leg1}, {"leg2", &bp.leg2}, {"arm1", &bp.arm1}, {"arm2", &bp.arm2},
                                  {"lefteye", &bp.lefteye}, {"righteye", &bp.righteye},
                                  {"lefteyebrow", &bp.lefteyebrow}, {"righteyebrow", &bp.righteyebrow},
                                  {"leftear", &bp.leftear}, {"rightear", &bp.rightear}, {"mouth", &bp.mouth}}) {
        w.key(part_name);
        w.begin_array();
        w.value(static_cast<uint64_t>(part->part_sprite_idx));
        w.value(static_cast<uint64_t>(part->texture_sprite_idx));
        w.value(static_cast<uint64_t>(part->scar_sprite_idx));
        w.end_array();
    }
    w.end_object();

    w.key("actives");
    w.begin_array();
    for(auto &s : cat->actives_basic) w.value(s.as_native_string_view());
    for(auto &s : cat->actives_accessible) w.value(s.as_native_string_view());
    for(auto &s : cat->actives_inherited) w.value(s.as_native_string_view());
    w.end_array();

    w.key("equipment");
    w.begin_object();
    for(auto [slot, eq] : {std::pair{"head", &cat->head}, {"face", &cat->face}, {"neck", &cat->neck}, {"weapon", &cat->weapon}, {"trinket", &cat->trinket}}) {
        w.key(slot);
        w.begin_object();
        w.kv("name", eq->name.as_native_string_view());
        w.kv("aux", eq->aux_string.as_native_string_view());
        w.kv("uses_left", eq->uses_left);
        w.end_object();
    }
    w.end_object();

    w.end_object();
}

// Very small hand-rolled tokenizer: splits on single spaces.
// (The command grammar is simple enough that this beats vendoring a full
// argument-parsing library -- see pipe_server.hpp for why we don't just
// send/parse JSON on the way in.)
std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while(start <= line.size()) {
        size_t space = line.find(' ', start);
        if(space == std::string_view::npos) {
            tokens.push_back(line.substr(start));
            break;
        }
        tokens.push_back(line.substr(start, space - start));
        start = space + 1;
    }
    return tokens;
}

std::string handle_request(std::string_view line) {
    JsonWriter w;
    auto tokens = tokenize(line);
    if(tokens.empty() || tokens[0].empty()) {
        w.begin_object().kv("ok", false).kv("error", std::string_view("empty command")).end_object();
        return w.str();
    }

    std::string_view cmd = tokens[0];

    // DAY: the day counter plus the house's food and gold.
    if(cmd == "DAY") {
        w.begin_object().kv("ok", true).kv("day", current_day());
        if(int32_t *money = house_money()) {
            w.kv("food", money[0]).kv("gold", money[1]);
        }
        w.end_object();
        return w.str();
    }

    // SET_FOOD <value>: set the house's food stock.
    if(cmd == "SET_FOOD" && tokens.size() >= 2) {
        int32_t value = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), value);
        int32_t *money = house_money();
        if(money == nullptr) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("HouseInventory not loaded")).end_object();
            return w.str();
        }
        if(value < 0) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("food must be >= 0")).end_object();
            return w.str();
        }
        int32_t previous = money[0];
        money[0] = value;
        w.begin_object().kv("ok", true).kv("previous", previous).kv("food", money[0]).end_object();
        return w.str();
    }

    if(cmd == "LIST_CATS") {
        auto cats = collect_all_cats();
        w.begin_object();
        w.kv("ok", true);
        w.kv("day", current_day());
        w.key("cats");
        w.begin_array();
        for(auto &[sql_key, found] : cats) {
            serialize_cat(w, found);
        }
        w.end_array();
        w.end_object();
        return w.str();
    }

    if(cmd == "GET_CAT" && tokens.size() >= 2) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        auto cats = collect_all_cats();
        auto it = cats.find(sql_key);
        if(it == cats.end()) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("cat not found")).end_object();
            return w.str();
        }
        w.begin_object();
        w.kv("ok", true);
        w.key("cat");
        serialize_cat(w, it->second);
        w.end_object();
        return w.str();
    }

    // Debug: every CatParts component in every scene, duplicates included,
    // to work out which ones correspond to cats the player actually owns.
    if(cmd == "CAT_SOURCES") {
        w.begin_object();
        w.kv("ok", true);
        w.key("sources");
        w.begin_array();
        for_each_cat_parts([&](Scene *scene, CatParts *parts) {
            w.begin_object();
            w.kv("scene", scene->name.as_native_string_view());
            w.kv("component_enabled", parts->enabled);
            w.kv("component_deleted", parts->deleted);
            w.kv("entity_enabled", parts->entity_enabled);
            w.key("entity_components");
            w.begin_array();
            if(parts->entity != nullptr) {
                for(auto p_sibling : parts->entity->components) {
                    if(p_sibling != nullptr) {
                        w.value(get_type_name(p_sibling));
                    }
                }
            }
            w.end_array();
            if(parts->cat != nullptr) {
                w.kv("sql_key", parts->cat->sql_key);
                w.kv("cat_ptr", reinterpret_cast<uint64_t>(parts->cat));
                w.kv("name", convert_utf16_wstring_to_utf8_string(parts->cat->name.as_native_wstring_view()));
                w.kv("dead", parts->cat->campaign_stats.dead);
                w.kv("deathday_house", parts->cat->deathday_house);
                w.kv("flags", parts->cat->flags);
            }
            w.end_object();
        });
        w.end_array();
        w.end_object();
        return w.str();
    }

    // Debug: hex dump of the first bytes of a component on a house cat's
    // entity, for reverse-engineering its layout. Clamped to readable memory.
    if(cmd == "DUMP_COMPONENT" && tokens.size() >= 3) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        std::string_view wanted_type = tokens[2];
        size_t length = 0x300;
        if(tokens.size() >= 4) {
            std::from_chars(tokens[3].data(), tokens[3].data() + tokens[3].size(), length, 16);
        }

        const void *target = nullptr;
        if(wanted_type == "@") {
            // "DUMP_COMPONENT <address> @ [len]": raw read at an address
            // taken from an earlier dump (e.g. to follow a pointer field).
            target = reinterpret_cast<const void *>(static_cast<uintptr_t>(sql_key));
        } else if(wanted_type.starts_with("#")) {
            // "DUMP_COMPONENT <n> #TypeName [len]": the n-th component of that
            // type across all scenes (not tied to a cat).
            std::string_view type = wanted_type.substr(1);
            int64_t n = 0;
            MewDirector *p_mewdirector = get_p_mewdirector_singleton();
            if(p_mewdirector != nullptr && p_mewdirector->director != nullptr) {
                for(auto p_scene : p_mewdirector->director->scenes) {
                    if(target != nullptr || p_scene == nullptr || p_scene->ComponentLists == nullptr) {
                        continue;
                    }
                    for(auto p_component : *p_scene->ComponentLists) {
                        if(get_type_name(p_component) == type && n++ == sql_key) {
                            target = p_component;
                            break;
                        }
                    }
                }
            }
            if(target == nullptr) {
                w.begin_object().kv("ok", false).kv("error", std::string_view("component not found")).end_object();
                return w.str();
            }
        }
        for_each_cat_parts([&](Scene *, CatParts *parts) {
            if(target != nullptr || parts->cat == nullptr || parts->cat->sql_key != sql_key || parts->entity == nullptr) {
                return;
            }
            for(auto p_sibling : parts->entity->components) {
                if(p_sibling != nullptr && get_type_name(p_sibling) == wanted_type) {
                    target = p_sibling;
                    return;
                }
            }
        });
        if(target == nullptr) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("component not found")).end_object();
            return w.str();
        }

        MEMORY_BASIC_INFORMATION mbi = {};
        auto base = reinterpret_cast<uintptr_t>(target);
        if(VirtualQuery(target, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
           (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("component memory not readable")).end_object();
            return w.str();
        }
        size_t readable = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - base;
        length = std::min(length, readable);

        static constexpr char HEX[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(length * 2);
        auto bytes = reinterpret_cast<const uint8_t *>(target);
        for(size_t i = 0; i < length; i++) {
            hex += HEX[bytes[i] >> 4];
            hex += HEX[bytes[i] & 15];
        }
        w.begin_object();
        w.kv("ok", true);
        w.kv("address", static_cast<uint64_t>(base));
        w.kv("length", static_cast<uint64_t>(length));
        w.kv("hex", hex);
        w.end_object();
        return w.str();
    }

    // Rooms with their summed furniture effects, the furniture in each, and
    // which cats live there.
    if(cmd == "ROOMS") {
        std::vector<HouseRoom *> rooms;
        std::vector<FurniturePiece *> pieces;
        MewDirector *p_mewdirector = get_p_mewdirector_singleton();
        if(p_mewdirector != nullptr && p_mewdirector->director != nullptr) {
            for(auto p_scene : p_mewdirector->director->scenes) {
                if(p_scene == nullptr || p_scene->ComponentLists == nullptr) {
                    continue;
                }
                for(auto p_component : *p_scene->ComponentLists) {
                    std::string type = get_type_name(p_component);
                    if(type == "FurnitureGrid") {
                        rooms.push_back(static_cast<HouseRoom *>(p_component));
                    } else if(type == "FurniturePiece") {
                        pieces.push_back(static_cast<FurniturePiece *>(p_component));
                    }
                }
            }
        }
        auto cats = collect_all_cats();

        w.begin_object();
        w.kv("ok", true);
        w.key("rooms");
        w.begin_array();
        for(HouseRoom *room : rooms) {
            w.begin_object();
            w.kv("name", room->name.as_native_string_view());
            w.key("effects");
            w.begin_object();
            for(const RoomEffect &e : room->effects) {
                w.kv(e.name.as_native_string_view(), e.value);
            }
            w.end_object();
            w.key("furniture");
            w.begin_array();
            for(FurniturePiece *piece : pieces) {
                if(piece->room != room || piece->instance == nullptr) {
                    continue;
                }
                w.begin_object();
                w.kv("id", piece->instance->id);
                w.kv("type", piece->instance->type.as_native_string_view());
                w.kv("x", piece->instance->x);
                w.kv("y", piece->instance->y);
                w.end_object();
            }
            w.end_array();
            w.key("cats");
            w.begin_array();
            for(auto &[sql_key, found] : cats) {
                if(found.house_cat != nullptr && found.house_cat->room == room) {
                    w.value(sql_key);
                }
            }
            w.end_array();
            w.end_object();
        }
        w.end_array();
        w.end_object();
        return w.str();
    }

    // Every cat the game has a pedigree record for (including dead / gone
    // cats): [sql_key, parent_a, parent_b, coi]. Parents are -1 if unknown.
    if(cmd == "PEDIGREE") {
        auto db = static_cast<CatDatabase *>(find_component("CatDatabase"));
        if(db == nullptr) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("CatDatabase not loaded")).end_object();
            return w.str();
        }
        const auto &table = db->pedigree;
        if(((table.capacity + 1) & table.capacity) != 0 || table.size > table.capacity) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("pedigree table looks corrupt (layout changed?)")).end_object();
            return w.str();
        }
        w.begin_object();
        w.kv("ok", true);
        w.kv("day", current_day());
        w.key("pedigree");
        w.begin_array();
        table.for_each([&](const PedigreeEntry &e) {
            w.begin_array();
            w.value(e.sql_key).value(e.parent_a).value(e.parent_b).value(e.coi);
            w.end_array();
        });
        w.end_array();
        w.end_object();
        return w.str();
    }

    // Debug: which loaded component lives at <decimal address>? Also lists
    // every component type name with counts, per scene.
    if(cmd == "COMPONENT_TYPES") {
        uint64_t wanted = 0;
        if(tokens.size() >= 2) {
            std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), wanted);
        }
        MewDirector *p_mewdirector = get_p_mewdirector_singleton();
        w.begin_object();
        w.kv("ok", true);
        w.kv("mewdirector", reinterpret_cast<uint64_t>(p_mewdirector));
        w.key("scenes");
        w.begin_array();
        if(p_mewdirector != nullptr && p_mewdirector->director != nullptr) {
            for(auto p_scene : p_mewdirector->director->scenes) {
                if(p_scene == nullptr || p_scene->ComponentLists == nullptr) {
                    continue;
                }
                std::map<std::string, int> counts;
                std::string match;
                for(auto p_component : *p_scene->ComponentLists) {
                    std::string name = get_type_name(p_component);
                    counts[name]++;
                    if(reinterpret_cast<uint64_t>(p_component) == wanted) {
                        match = name;
                    }
                }
                w.begin_object();
                w.kv("scene", p_scene->name.as_native_string_view());
                w.kv("match", match);
                w.key("types");
                w.begin_object();
                for(auto &[name, n] : counts) {
                    w.kv(name, static_cast<int32_t>(n));
                }
                w.end_object();
                w.end_object();
            }
        }
        w.end_array();
        w.end_object();
        return w.str();
    }

    if(cmd == "DUMP_CAT" && tokens.size() >= 2) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        auto cats = collect_all_cats();
        auto it = cats.find(sql_key);
        if(it == cats.end()) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("cat not found")).end_object();
            return w.str();
        }
        w.begin_object();
        w.kv("ok", true);
        w.key("cat");
        serialize_cat_debug(w, it->second.cat);
        w.end_object();
        return w.str();
    }

    if(cmd == "SET_STAT" && tokens.size() >= 4) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        std::string_view stat_name = tokens[2];
        int32_t value = 0;
        std::from_chars(tokens[3].data(), tokens[3].data() + tokens[3].size(), value);

        auto cats = collect_all_cats();
        auto it = cats.find(sql_key);
        if(it == cats.end()) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("cat not found")).end_object();
            return w.str();
        }
        CatData *cat = it->second.cat;

        // NOTE: this writes directly to stats_heritable (the "base" stat),
        // not the visible total. That's a deliberate simplification for the
        // MVP -- decide in Claude Code whether you'd rather this adjust
        // stats_delta_levelling instead, depending on how you want it to
        // interact with the game's own level-up UI.
        int32_t *target = nullptr;
        if(stat_name == "str") target = &cat->stats_heritable.str;
        else if(stat_name == "dex") target = &cat->stats_heritable.dex;
        else if(stat_name == "con") target = &cat->stats_heritable.con;
        else if(stat_name == "int") target = &cat->stats_heritable.int_;
        else if(stat_name == "spd") target = &cat->stats_heritable.spd;
        else if(stat_name == "cha") target = &cat->stats_heritable.cha;
        else if(stat_name == "lck") target = &cat->stats_heritable.lck;

        if(target == nullptr) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("unknown stat name")).end_object();
            return w.str();
        }

        *target = value;
        w.begin_object();
        w.kv("ok", true);
        w.key("cat");
        serialize_cat(w, it->second);
        w.end_object();
        return w.str();
    }

    // SET_PASSIVE <sql_key> <slot> <key> <level>: replace a passive or a
    // disorder. slot: passive1, passive2, disorder1, disorder2 (CatData's
    // passive_0/1 and mutation_0/1). key "None" clears the slot. The key is
    // validated against game data by the MCP server, not here.
    if(cmd == "SET_PASSIVE" && tokens.size() >= 5) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        std::string_view slot = tokens[2];
        std::string_view key = tokens[3];
        int64_t level = 1;
        std::from_chars(tokens[4].data(), tokens[4].data() + tokens[4].size(), level);

        auto cats = collect_all_cats();
        auto it = cats.find(sql_key);
        if(it == cats.end()) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("cat not found")).end_object();
            return w.str();
        }
        CatData *cat = it->second.cat;
        MsvcReleaseModeXString *name = nullptr;
        int64_t *lvl = nullptr;
        if(slot == "passive1") { name = &cat->passive_0; lvl = &cat->passive_0_level; }
        else if(slot == "passive2") { name = &cat->passive_1; lvl = &cat->passive_1_level; }
        else if(slot == "disorder1") { name = &cat->mutation_0; lvl = &cat->mutation_0_level; }
        else if(slot == "disorder2") { name = &cat->mutation_1; lvl = &cat->mutation_1_level; }
        if(name == nullptr || key.empty() || level < 1) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("bad slot, key or level")).end_object();
            return w.str();
        }

        std::string previous(name->as_native_string_view());
        int64_t previous_level = *lvl;
        // Free the old buffer (if heap-allocated) and build the new string on
        // the process heap, which the game's CRT also allocates from.
        name->destroy();
        name->construct(key.data(), key.size());
        *lvl = level;

        w.begin_object();
        w.kv("ok", true);
        w.kv("previous", previous);
        w.kv("previous_level", previous_level);
        w.key("cat");
        serialize_cat(w, it->second);
        w.end_object();
        return w.str();
    }

    // SET_PART <sql_key> <part> <sprite_idx>: swap a body part's sprite index
    // (>= 300 = mutation, -2 = missing part). Only the index is written; the
    // part's own texture/scar indices are left alone.
    if(cmd == "SET_PART" && tokens.size() >= 4) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        std::string_view part_name = tokens[2];
        int32_t value = 0;
        std::from_chars(tokens[3].data(), tokens[3].data() + tokens[3].size(), value);

        auto cats = collect_all_cats();
        auto it = cats.find(sql_key);
        if(it == cats.end()) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("cat not found")).end_object();
            return w.str();
        }
        BodyParts &bp = it->second.cat->body_parts;
        uint32_t *target = nullptr;
        if(part_name == "texture") target = &bp.texture_sprite_idx;
        for(auto [name, part] : {std::pair{"body", &bp.body}, {"head", &bp.head}, {"tail", &bp.tail},
                                 {"leg1", &bp.leg1}, {"leg2", &bp.leg2}, {"arm1", &bp.arm1}, {"arm2", &bp.arm2},
                                 {"lefteye", &bp.lefteye}, {"righteye", &bp.righteye},
                                 {"lefteyebrow", &bp.lefteyebrow}, {"righteyebrow", &bp.righteyebrow},
                                 {"leftear", &bp.leftear}, {"rightear", &bp.rightear}, {"mouth", &bp.mouth}}) {
            if(part_name == name) target = &part->part_sprite_idx;
        }
        if(target == nullptr) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("unknown part name")).end_object();
            return w.str();
        }

        int32_t previous = static_cast<int32_t>(*target);
        *target = static_cast<uint32_t>(value);
        w.begin_object();
        w.kv("ok", true);
        w.kv("previous", previous);
        w.key("cat");
        serialize_cat(w, it->second);
        w.end_object();
        return w.str();
    }

    if(cmd == "SET_HP" && tokens.size() >= 3) {
        int64_t sql_key = 0;
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), sql_key);
        int32_t value = 0;
        std::from_chars(tokens[2].data(), tokens[2].data() + tokens[2].size(), value);

        auto cats = collect_all_cats();
        auto it = cats.find(sql_key);
        if(it == cats.end()) {
            w.begin_object().kv("ok", false).kv("error", std::string_view("cat not found")).end_object();
            return w.str();
        }
        it->second.cat->campaign_stats.hp = value;

        w.begin_object();
        w.kv("ok", true);
        w.key("cat");
        serialize_cat(w, it->second);
        w.end_object();
        return w.str();
    }

    w.begin_object().kv("ok", false).kv("error", std::string_view("unknown command")).end_object();
    return w.str();
}

void on_update_frame() {
    std::string request;
    if(P.pipe.try_pop_request(request)) {
        std::string response = handle_request(request);
        P.pipe.push_response(std::move(response));
    }
}

// Hook MewDirector's always_update routine to service one pending pipe
// request per frame, on the game's own thread. See pipe_server.hpp for why.
MAKE_SHOOK(0, ADDRESS_glaiel__MewDirector__always_update,
    void, __cdecl, glaiel__MewDirector__always_update,
    MewDirector* thiss
) {
    on_update_frame();
    glaiel__MewDirector__always_update_hook.orig(thiss);
}

// Called from amoeboid.cpp's on_attach(), after hooks are installed.
void cat_bridge_on_attach() {
    P.pipe.start();
}

// Called from DllMain on detach, under the loader lock, so it must not join
// the pipe thread. On a live unload, CatBridgeShutdown should have run first.
void cat_bridge_on_detach() {
    P.pipe.abandon();
}

// Exported; an injector runs this via CreateRemoteThread before FreeLibrary
// so the pipe thread is joined outside the loader lock.
extern "C" DWORD WINAPI CatBridgeShutdown(LPVOID) {
    P.pipe.stop();
    return 0;
}
