# Mewgenics Cat Bridge -- developer notes

Everything a contributor (or a fresh Claude Code session) needs: how it
works, what has been reverse-engineered and verified in-game, what is only
a hypothesis, and how to work on it. For installing and using the tool,
see the [README](../README.md).

## What this is

An MCP server that lets an LLM (Claude) read and edit live game state in a
running copy of **Mewgenics** (the cat-breeding roguelike by Edmund
McMillen / Tyler Glaiel), by:

1. Injecting a small C++ DLL (`mod/cat_bridge/`) into the game process.
   It walks the game's own in-memory data structures (an ECS: Scenes ->
   Components -> `CatParts` -> `CatData`, plus `CatDatabase`,
   `FurnitureGrid`, ...) and exposes them over a local Windows named pipe
   (`\\.\pipe\cat_bridge`, one text command line in, one JSON line out).
2. Running a Python MCP server (`mcp_server/server.py`) that talks to that
   pipe, combines the raw data with the game's own data files
   (`resources.gpak`) and exposes cats, pedigree, rooms, breeding and
   adventure advice as MCP tools.

## Where this came from / why it's plausible at all

Mewgenics devs have said official mod support is planned but not out yet.
In the meantime the community has reverse-engineered a lot already, all
under permissive licenses:

* **Mewtator** (https://www.nexusmods.com/mewgenics/mods/1) -- mod
  manager, loads mods via `-modpaths`.
* **Mewjector** (https://www.nexusmods.com/mewgenics/mods/218) -- DLL
  loader, proxies `version.dll`.
* **Mewgenics Mod Framework** (https://www.nexusmods.com/mewgenics/mods/183)
  by Pilout -- a C#/.NET modding SDK with an in-game F1 overlay. We did
  **not** use this one; see "Alternative approach" below.
* **mewgenics_randomize_item_picks**
  (https://github.com/p0lymeric/mewgenics_randomize_item_picks) by
  p0lymeric, MIT licensed -- a small C++ mod that hooks the game via
  signature scanning + Detours, with fully reverse-engineered struct
  layouts for `CatData`, the ECS (`Entity`/`Component`/`Scene`/`Director`),
  UI components, etc. **This repo is the direct basis for `mod/cat_bridge/`
  in this project** -- `amoeboid.cpp`, everything under `types/` and
  `utilities/`, and `misc/find_rvas.py` are copied/adapted from it with
  minimal changes. Go read that repo's source if anything here is unclear;
  it's the best reference available for exactly this game.

## Repo layout

```
mod/                    C++ DLL (the "mod" side)
  build.ps1              build (finds VS's CMake), copies the DLL to ../bin
  CMakeLists.txt         top-level build file
  lib/                   vendored third-party libs (Detours, libtomcrypt, mewjector, SDL headers)
  cat_bridge/
    amoeboid.cpp          generic DLL load/hook/error-handling boilerplate (upstream)
    amoeboid.hpp          mod metadata + the 2 function/data signatures we need
    cat_bridge.cpp        <-- the pipe commands live here. Start reading here.
    pipe_server.hpp/.cpp   named pipe I/O on a background thread
    json_writer.hpp        tiny hand-rolled JSON writer (no parser needed)
    types/glaiel_*.hpp      reverse-engineered game structs (upstream + ours:
                            glaiel_house.hpp, glaiel_catdb.hpp)
    utilities/*             hooking/signature-scanning/logging helpers (upstream)
    misc/find_rvas.py       regenerate signatures after a game update
  ATTRIBUTION.md, LICENSE.md

mcp_server/             Python MCP server (the "brain" side)
  server.py               MCP tool definitions, pipe client
  game_data.py            resources.gpak / .gon parsing, stat totals, abilities, furniture
  breeding.py             pedigree, kinship/COI, pair evaluation, multi-generation plans
  rooms.py                room effects, crowding, furniture suggestions
  advisor.py              breeding roles, adventure team, stray evaluation

tools/
  cat_bridge_dev.py       inject / eject the DLL, send raw pipe commands
  gpak.py                 list / extract files from resources.gpak
  package_release.py      build the release zip

docs/                   this file
```

## Pipe commands (DLL)

Read: `LIST_CATS`, `GET_CAT <key>`, `PEDIGREE`, `ROOMS`, `DAY` (day, food,
gold; LIST_CATS and PEDIGREE also carry `day`).
Write: `SET_STAT <key> <stat> <value>`, `SET_HP <key> <value>`,
`SET_PART <key> <part> <sprite_idx>`, `SET_FOOD <value>`,
`SET_FURNITURE_EFFECT <type> <effect> <value>`, `SET_ROOM_EFFECT <room> <effect> <value>`
(the latter is useless: see below),
`SET_PASSIVE <key> <passive1|passive2|disorder1|disorder2> <name> <level>`.
Debug: `DUMP_CAT <key>`, `CAT_SOURCES`, `COMPONENT_TYPES [addr]`,
`DUMP_COMPONENT <key> <Type> [hexlen]` / `<n> #<Type>` / `<addr> @`.
Every command runs on the game thread inside the `MewDirector::always_update`
hook (one per frame); the pipe thread only does I/O.

## Findings log (what's verified and how)

Verified on Windows 11 against Mewgenics 1.1.21239 (Steam, SHA-256 matches
`amoeboid.hpp` as shipped -- no signature changes were needed):

* Builds cleanly with VS 2026 (MSVC 19.51) via the bundled CMake.
* Loads without Mewtator/Mewjector via `tools/cat_bridge_dev.py inject`
  (falls back to Detours). `eject` + re-`inject` works repeatedly without
  restarting the game -- that's the dev loop.
* `LIST_CATS` returns correct names (UTF-16 -> UTF-8 fine), levels, stats.
* Displayed stat = base + levelling + injuries + collar class `stat_mods`
  + passive/disorder `stats` (per level). Verified on 4 cats / 4 classes.
  Item stat bonuses are applied but not yet verified; body-part mutations
  are handled too (see below).
* The House scene also holds CatParts for UI-only cats: a `CatStatsDrawer`
  panel carries 3 CatParts (a cat + its parents, possibly long dead), all
  sharing the real cat's `CatData*`. Cats in the house scene have a
  `HouseCat` component on the same entity; `HouseCat::room` (+0xe8, see
  `types/glaiel_house.hpp`) points to the room, whose name string sits at
  +0x40. The daily stray waiting outside has a HouseCat but `room == nullptr`
  (reported as `outside`). `in_house` = room != nullptr; verified 1:1 against
  the save's `house_state` roster in two save slots (6 and 19 cats).
* `stats_delta_levelling` is really "persistent bonus": level-0 cats can
  have +1 there (events), and the game shows it as Bonus, not Base.
* Body-part mutations: `BodyPartDescriptor::part_sprite_idx` (and
  `BodyParts::texture_sprite_idx`) index `data/mutations/<group>.gon`;
  ids >= 300 are mutations, -2 is a missing part (birth defect), lower ids
  are normal parts with no effect. Handled in `game_data.cat_mutations()`.
  Verified in-game by live-editing sprite indices with `SET_PART` (MCP tool
  `set_cat_body_part`): the game re-renders the part and updates stats
  immediately. A mutation counts once per slot (horns on both ears = +1);
  arms and legs are separate slots that both read legs.gon (hooves on arms
  + legs = +2, on both arms + one leg = +2). Still unseen: two different
  mutations in one slot (left vs right ear).
* Pedigree is live in memory: component `CatDatabase` (scene "Shared"),
  layout in `types/glaiel_catdb.hpp`. `PEDIGREE` returns every cat ever
  [sql_key, sire, dam, coi], identical to the save's `pedigree` blob.
  parent_a = sire (sex 0 = male only appears there), parent_b = dam (sex 1
  = female); sex 2 is shown as "?" in-game and can be either parent
  (confirmed by the player; appears in both roles). `mcp_server/breeding.py`
  recomputes kinship/COI and matches the game's stored COI on all 256 cats
  of the test save. MCP tools: `get_family`, `evaluate_pair`,
  `suggest_breeding_pairs`. Unverified: whether "?" x "?" can breed, what
  `sexuality` means (~1.0 on 2 cats).
* LIST_CATS also returns breeding fields straight from CatData: sex, coi,
  birthday, libido, sexuality, fertility, aggression, lover/hater + affinity.
* JsonWriter doubles now use shortest round-trip `std::to_chars` (the old
  `std::to_string` lost precision and could emit invalid `nan`).
* Rooms: component "FurnitureGrid" is the room (same object HouseCat::room
  points to); +0x140 is a vector of {string effect, double value} = exact
  sum of its furniture's effects from data/furniture_effects.gon.
  In-game Comfort = that total - max(0, cats_in_room - 4) (verified on 3
  rooms; matches the community guide). "FurniturePiece" components point
  to their room (+0x48) and a FurnitureInstance record (+0x2d8: id, type,
  room, x, y). DLL command `ROOMS`; layouts in `types/glaiel_house.hpp`.
  MCP tools `get_rooms`, `suggest_room_setup`. The breeding effects listed
  in furniture_effects_guide.gon (InheritStatFavorBest, KittenChanceOf...,
  IncreaseFertility, ...) are used by NO furniture in this version.
* Body parts are inherited per slot from one parent (both ears from the
  same parent etc.; checked on 3 live families -- eyebrows matched neither
  parent in 2 of them, so they may be re-rolled/mutated).
* Community guide used as a secondary source (Steam guide id 3667300706,
  RU): Stimulation -> kitten takes the better stat & passes mutations/
  abilities more often; gay cats only breed with "?" cats (we treat
  sexuality > 0.5 as gay -- values seen are <= 0.10 or >= 0.93); its poop
  value (-1 Comfort) disagrees with the game data (-2), data wins.
* Breeding MCP tools also: `plan_breeding` (greedy multi-generation plan
  towards 7s, assuming high Stimulation).
* `mcp_server/advisor.py` (heuristics, reasons included in every verdict):
  `breeding_roles` (keep for breeding vs free for adventures),
  `suggest_adventure_team` (party from non-breeders + a collar each; class
  fit uses data/classes stat_mods and levelup_stats as the class's key
  stats), `evaluate_strays` (adopt today's stray?). The stray waiting
  outside is part of the breeding pool in pairs/plan tools.
  Unlocked advanced classes aren't read yet (default = 6 base classes).
* Breeding model from https://github.com/jph6366/mewgenics-mcp (save-file
  based) checked against our data on 2026-09-25:
  - its COI formula `0.5*(F1+F2) + kinship` is WRONG: the game's COI is
    exactly kinship(sire, dam) (our breeding.py, verified on 256 cats).
  - stimulation thresholds (>=196 always better stat, >=95 70%, >=32 55%,
    else 50/50) are UNVERIFIED but consistent with what we saw (best room
    has Stimulation 13, kittens got lower stats about half the time).
    DONE: breeding.py uses these tiers (STIMULATION_TIERS) for
    `expected` stats, `chance_of_best_kitten`, kittens needed, and
    plan_breeding's what-if table; all flagged as a hypothesis in tool docs.
    Stimulation used = the pair's shared room, else the best room;
    overridable. Unknown whether the game uses the room's or the house's
    total Stimulation.
  - 20% per-part mutation re-roll: consistent with eyebrows matching
    neither parent in 2 of 3 families. Birth-defect chance
    `0.02 + 0.4*max(F-0.2, 0)` and 15%/parent disorder inheritance:
    unverified.
* Abilities (2026-09-25): LIST_CATS returns `actives` {basic, accessible,
  inherited} as internal keys; game_data.ability_info/passive_info map them
  to localized names/descriptions (text/combined.csv has an `ru` column).
  `actives_inherited` = the innate ability the cat was born with (every
  one of 141 kittens in the save had accessible[0] == inherited[0]; strays
  can differ, e.g. a stray given a class ability). Inheritance measured on
  those 141 families: 66% random new, 32% a parent's innate, 2% a parent's
  LEARNED ability (ABILITY_INHERITANCE_OBSERVED). No cat in that save had
  passives at birth -- they come from levels/runs; passive inheritance is
  unmeasured (guide: ~10% at Stimulation 40). Decoding passives/disorders
  from save blobs by token order was unreliable -- don't trust it.
  evaluate_pair has `kitten_active_ability`; adventure team lists abilities.
* `SET_PASSIVE <sql_key> <passive1|passive2|disorder1|disorder2> <key>
  <level>` (MCP `set_cat_passive`, plus `find_passives` to look keys up):
  frees the old MSVC string and constructs the new one on the process heap
  (host_alloc = the game CRT's heap). Verified in-game 2026-09-25: gave a
  cat ToadStyle lvl 1 -> passive shown, SPD +4 as predicted; clearing
  ("None") restored it. Survives the game's own save + reload (checked
  in-game by the player, 2026-09-25).
  Passive entries in LIST_CATS now carry their `slot`.
* Mutations by effect (2026-09-25, Python only, no DLL change): MCP
  `find_mutations` searches data/mutations/*.gon by localized description
  (`desc` key -> text/combined.csv), tag or stat; mutations have no names of
  their own (combined.csv only has per-slot names like MUTATION_EAR_NAME).
  `add_cat_mutation` writes the id into every part of the slot via
  `SET_PART` (both ears / both arms, or one side) and returns the old
  indices. Verified in-game 2026-09-25: ears 303 on both ears of a cat ->
  new ears drawn, STR 6 -> 7 (once per slot, as predicted), knockback
  effect shown in the cat's info (checked by the player). Survives the
  game's own save + reload (ears still 303/303 after reloading the save).
  Tags seen: animal, common (the 400-449 block: +2/-1 stat
  pairs), bird, extra, melted, birth_defect (-2 and most of 700+).
  Mutations with only a `passives` block (e.g. Thorns) are assumed to take
  effect like stat ones, since the game keys everything by sprite index --
  not yet checked in combat.
* Cat info tab labels (2026-09-25), read from Mewgenics.exe 1.1.21239:
  the function around RVA 0xe3ee0-0xe4940 (found via code refs to the
  HOUSE_CAT_INFO_* text keys) compares CatData doubles to constants and
  picks the label. libido (+0xbb8) and aggression (+0xbe8): < 0.3 low,
  > 0.7 high, else mid. coi (+0xc50): > 0.1 / 0.25 / 0.5 / 0.8 ->
  INBRED1..4 (Лёгкое / Среднее / Высокое / Королевское), else INBRED0.
  sexuality (+0xbc0): < 0.1 straight, > 0.9 gay, else bi (icon only).
  Offsets match `types/glaiel_cat.hpp`. Checked against the UI: cat 226
  (libido 0.534, aggression 0.367, coi 0) = Среднее либидо / Средняя
  агрессия / Без вырождения; near-threshold cats checked by the player
  too: aggression 0.701 = Высокая, coi 0.103 = Лёгкое, coi 0.302 =
  Среднее, libido 0.188 = Низкое, sexuality 0.997 / 0.935 = gay icon.
  `enrich_cat` adds `temperament`;
  breeding.orientation and inbreeding_level now use these thresholds
  (the old 0.5 gay cut-off was a guess). Whether bi cats breed with
  anyone, and what libido/aggression actually do, is unverified.
* Mating (2026-09-25), read from Mewgenics.exe 1.1.21239 code, NOT yet
  checked against observed births (full .text linear sweep with capstone,
  then refs to the CatData offsets):
  - 0xd2850 attraction(A, B) = 0 if same cat, if 0xd3130(B) or bit 21 of
    either cat's flags (+0xbf8) is set (meaning unknown); else
    libido_A * orient * CHA_B * 0.15 * lover. orient: sincos of
    sexuality_A * pi/2 (0xda7bf0): cos towards the other sex, sin towards
    the same sex, 1 (the vector length) if either cat is sex 2 "?"
    (straight cats breed with the other sex, so cos must be the other-sex
    term). CHA_B = int at +0x14 of the stat block returned by 0xc1820
    (CatStats order -> cha; which total -- base or displayed -- not
    checked). lover: if A has a lover (+0xbc8), * (1 + affinity +0xbd0)
    when B is the lover, else * (1 - affinity).
  - 0xd2ab0 roll(A, B, f): p = attraction * f, false if <= 0, true if >= 1,
    else random. The nightly breeding loop (~0x1e9b44) picks a partner B
    for A (0x1f21a0, not decoded) and needs roll(A,B) AND roll(B,A) with
    f = sqrt(room +0x120).
  - room +0x120 (0x2ea8c0) = 1 - 0.1 * max(0, cats - 4) + 0.1 * each
    furniture "Comfort" (effect case 0x1c in 0x1b4930) = 1 + 0.1 * displayed
    Comfort. Negative -> sqrt NaN -> no breeding.
  - 0x1ea05f litter: p = fertility_A * fertility_B (+0xbf0); one kitten
    with chance p, a second with chance p - 1. Same-sex pair (neither "?")
    -> 0 kittens.
  - 0xd29e0 fight score(A, B) = (aggression_A + hate + 0.25 if flags bit 2)
    * (1 - 2.667 * attraction(A, B)); hate = +hater_affinity if B is A's
    hated cat, else -hater_affinity. Used by the partner/fight picker; the
    step from score to an actual fight isn't decoded.
  breeding.attraction / mating_outlook / fight_tendency implement this;
  evaluate_pair returns `mating`, suggest_breeding_pairs skips pairs under
  min_mating_chance. To verify: log nightly births per pair vs predicted.
* Day counter (2026-09-25): the save property `current_day` lives at
  MewDirector + 0x580 (int64). Found in Mewgenics.exe: the save loader
  (~RVA 0x3a6db9) reads "current_day" and stores it at [r12 + 0x580]; the
  breeding loop reads [MewDirector singleton (RVA 0x13dac30) + 0x580] (day 0
  forces mating). Live value 97 = `current_day` 97 in a copy of the save.
  Goes up by one per in-game night (97 -> 98 observed live, 2026-09-25).
* Room effect totals (FurnitureGrid + 0x140) are recomputed from the
  furniture continuously: `SET_ROOM_EFFECT` changed Floor1_Small
  Stimulation 13 -> 200 and the next read was 13 again. They are computed
  from data/furniture_effects.gon as loaded in memory: a GON tree at
  SpawnDatabase + 0xd48 (loader ~RVA 0x7a79cb copies it there). GON node
  layout in `types/glaiel_house.hpp` (GonObject, 0xb0 bytes: children
  vector +0x38, int +0x50, double +0x58, text +0x68, key +0x88, type +0xa8;
  found by scanning the process for the monitor's name/desc strings).
  `SET_FURNITURE_EFFECT object_electronics_monitor Stimulation 188` (the
  only monitor is in Floor1_Small) -> the room showed Stimulation 200 right
  away and kept it. The breeding code reads the same totals (Comfort via
  the room's effect list, 0x2ea8c0), so this is how experiments get high
  Stimulation. Lasts until the game restarts.
* Birth-log results, days 97-111 (14 nights, 49 kittens, one save;
  rooms of 9 / 9 / 2 cats, Comfort 3 / 3 / 8, Stimulation 13 / 4 / 0):
  - Litter size CONFIRMED: 42 matings, mean 1.17 kittens vs 1.20 predicted
    by p = fertility_A * fertility_B (one kitten w.p. p, twins w.p. p - 1).
  - Isolated pair (only two cats in the attic): a pair with per-attempt
    chance 0.56-0.83 bred 5 of 5 nights; a pair with 0.10-0.12 bred 3 of 7
    nights. Both fit "each of the two cats makes one attempt per night":
    P(night) = 1 - (1 - q)^2 = 0.81-0.97 and ~0.21 (3 of 7 is a bit high,
    P(>=3) ~ 0.17). Consistent with the model; not yet a precise check.
  - Shared rooms: pairs breed less often than q (e.g. q 0.2-0.3 -> 14% of
    nights): each cat picks one partner per night (0x1f21a0, not decoded).
  - Stimulation: kitten took the better parent's base stat in 98 of 171
    stat comparisons (57%, z = 1.9 vs 50%); by room Stimulation 0 / 4 / 13:
    71% (n 28) / 57% (n 61) / 52% (n 82) -- no upward trend, so nothing
    speaks for Stimulation mattering below 32; the 32 / 95 / 196 tiers
    remain untested (the house only has 17 Stimulation in total).
* House food / gold (2026-09-25): int32 at HouseInventory + 0xb0 / + 0xb4
  (save properties house_food / house_gold). Found in Mewgenics.exe: the
  house loader (~RVA 0x2067dd, where the compiler copies "house_food" with
  movsd) calls the property getter 0x22c5e0 (default 25) and stores the
  result at [obj + 0xb0], the next property at [obj + 0xb4]; the saver
  (~0x2060ba) reads them back from there. Checked live: gold 33 = save;
  food 97 (save) -> 78 after one night with 19 house cats, i.e. 1 food per
  cat per night. `SET_FOOD` (MCP `set_house_food`) set 78 -> 600 live: the
  UI showed 140/140 (storage capacity; 40 of it from FoodStorage furniture)
  and the night clamped it: 600 -> 140 - 20 cats = 120. So food above the
  capacity is lost overnight; where the capacity lives isn't found yet.
  MCP `get_house_status` reports day, food, gold and nights of food left.
* Birth log (`mcp_server/birth_log.py`, MCP `birth_log_report`, standalone
  `tools/birth_logger.py`): polls PEDIGREE + LIST_CATS + ROOMS every 10 s
  and writes one JSONL record per night to
  %LOCALAPPDATA%\mewgenics-cat-bridgeirth_log.jsonl (MEWGENICS_BIRTH_LOG
  = path or "off"). The pre-night state is the last snapshot before any new
  kitten (with parents) appeared; a night is closed one poll after the day
  changes, so it works whichever the game does first. Records: rooms
  (Comfort, Stimulation, cats), compact cats, every co-housed pair with the
  predicted mating chance, births (parents, parents' room, kitten stats /
  body parts / innate). Nights where the day jumped by more than one are
  marked `gap`. Save slots are told apart by a hash of the oldest pedigree
  rows. `send_command` now serializes requests (lock) and retries while the
  pipe is busy / between instances (errors 2, 231). First live night
  (day 97 -> 98): logged one kitten (257 from 210 x 202, Floor1_Small) and
  correctly skipped the stray that arrived the same morning. The logger
  keeps its last 20 state changes (`recent_events` in the report;
  tools/birth_logger.py prints them).
  Kittens are cached on the first poll they appear in, so a kitten the
  player removes right away still gets its stats logged (6 nights on
  2026-09-25 lost 7 of 25 kittens' stats before this).
* `tools/cat_bridge_dev.py eject` reads the shutdown export from the DLL
  file that is actually loaded (it may come from another checkout).
* Debug: `COMPONENT_TYPES [decimal addr]` lists component types per scene
  (and which one is at addr).
* Debug: `DUMP_COMPONENT <sql_key> <TypeName> [hexlen]` hex-dumps a
  component on a cat's entity; `DUMP_COMPONENT <decimal addr> @ [hexlen]`
  dumps raw memory. Both clamp to the readable region via VirtualQuery.

* `SET_STAT` works: edited `stats_heritable` shows in-game immediately and
  survives the game's own save + reload.
* `stats_heritable` is what kittens inherit (the game's "Base" stats, per
  NPC Tink's dialogue). Checked against the save's pedigree: each kitten
  stat equals one parent's heritable value; levelling deltas are never
  inherited.

NOT yet tested: `SET_HP`, other screens than the house.

Save-file notes (read-only research, not used by the tool): `cats.data` =
u32 raw size + LZ4 block; stats sit as 3 x 7 x i32 (heritable, levelling,
injuries) at a name-dependent offset (~454-478). `files.pedigree` starts
with records {i64 child, i64 parent_a, i64 parent_b, f64 coi} at 0xa8, but
only the first few are contiguous -- the rest looks like a hash table, not
yet decoded.

Additions since the original skeleton:
* `CatBridgeShutdown` export: stops the pipe thread outside the loader
  lock (joining it from DllMain would deadlock on eject).
* Pipe: late responses after a 2s timeout are dropped instead of being
  handed to the next client.
* Debug commands: `DUMP_CAT <key>` (all stat-relevant fields),
  `CAT_SOURCES` (every CatParts, with scene/flags/pointer).
* `tools/gpak.py` + `mcp_server/game_data.py`: read `resources.gpak`
  (format documented in game_data.py) and parse `.gon` game data.
* Save file: SQLite at `%APPDATA%\Glaiel Games\Mewgenics\<steamid>\saves\`
  with tables `cats` (every cat ever, blob per cat), `files` (named blobs:
  `house_state`, `pedigree`, `inventory_*`, ...), `properties` (key/value).
  `house_state` = u32 0, u32 count, count x {u64 sql_key, u64 len,
  room[len], 3 x f64}. Not used by the tool (live memory is enough), but
  handy for cross-checking.

## Development workflow

### Prerequisites

* Windows, Mewgenics (Steam).
* Visual Studio 2022+ or its Build Tools with "Desktop development with
  C++" (ships MSVC and CMake).
* 64-bit Python 3.10+: `pip install -r requirements.txt` (repo root).

### Build, load, iterate

```
powershell -ExecutionPolicy Bypass -File mod\build.ps1 -Console   # -Console: log window in the game
python tools\cat_bridge_dev.py inject      # game running, save loaded
python tools\cat_bridge_dev.py send LIST_CATS
python tools\cat_bridge_dev.py eject       # before rebuilding; no game restart needed
```

`inject` copies the DLL to `tools/.live/` first so the build output isn't
locked. No Mewtator/Mewjector is needed: without Mewjector the mod hooks
through Detours and can self-eject. (With Mewjector present it uses
Mewjector's coordinated hooking instead and can't be ejected.)

### After a game update

The DLL checks `Mewgenics.exe`'s SHA-256 against `EXE_SHA256` in
`mod/cat_bridge/amoeboid.hpp` and stays inactive (harmlessly) on a
mismatch. To update:

```
python mod\cat_bridge\misc\find_rvas.py     # finds the game through Steam, or pass the exe path
```

Paste the printed `EXE_SHA256` into `amoeboid.hpp` and bump `EXE_VERSION`.
The two symbols are resolved at runtime by signature scan, so as long as
the script finds each signature exactly once, nothing else changes. If one
comes back `<NOT FOUND>` / `<MULTIPLE MATCHES>`, the function changed and
the hex pattern must be redone in a disassembler (Ghidra). Struct offsets
(`types/*.hpp`) may also move between versions -- the `static_assert`s only
catch size changes, so re-verify with the debug commands.

### Verifying reverse-engineered layouts

The approach used throughout: form a hypothesis from a hex dump
(`DUMP_COMPONENT`), cross-check it against an independent source (the
save file, the game's UI, the `.gon` data), and only then promote it to a
struct in `types/`. The save file (`%APPDATA%\Glaiel Games\Mewgenics\<id>\saves\*.sav`,
SQLite) is used for validation only; the tool reads live memory.

## Known limitations

* Cats are found through loaded scenes; tested on the house screen. Other
  screens (adventure, map) are untested.
* One request in flight, one pipe client at a time.
* Writes are live-memory edits. Stat, passive and body-part (mutation)
  edits survive the game's own save + reload; `SET_HP` is untested.
* No equipment editing, no adding/removing cats, no active-ability editing
  yet (the fields are in `types/glaiel_cat.hpp`; `SET_PASSIVE` shows how
  to write an MSVC string safely).
* Not read yet: unlocked advanced classes, furniture in storage.
* Breeding probabilities by Stimulation are a hypothesis (from https://github.com/jph6366/mewgenics-mcp); ability inheritance odds were measured on one
  save.

## Alternative approach that was considered and NOT used

Pilout's **Mewgenics Mod Framework** (C#/.NET, NativeAOT) was the other
option -- it has a nicer developer experience (proper mod lifecycle, F1
overlay, IntelliSense SDK) but as of this writing it wasn't clear from its
Nexus page alone whether its SDK exposes direct access to game data
structures, or only lifecycle hooks (`OnLoad`/`OnEnable`/`OnDisable`). The
C++ signature-scanning approach was chosen instead because
`mewgenics_randomize_item_picks` already had fully worked-out, provably-
correct struct layouts and ECS traversal code to build on. If the C++
route turns out to be too fragile across game updates, revisiting the C#
framework (its docs are linked from
https://www.nexusmods.com/mewgenics/mods/183) is a reasonable pivot --
you'd port the same `CatData` struct layout, just accessed via .NET
marshaling instead of raw pointers.

## License note

`mod/` is built on MIT-licensed code from
https://github.com/p0lymeric/mewgenics_randomize_item_picks. See
`mod/LICENSE.md` and `mod/ATTRIBUTION.md`. If this is ever shared/published,
keep that attribution intact (the release zip ships both files).
