"""
Mewgenics Cat Bridge -- MCP server.

Talks to the cat_bridge.dll mod (see ../mod/) over a Windows named pipe,
and exposes what it finds as MCP tools an LLM can call.

Wire protocol (must match mod/cat_bridge/pipe_server.hpp):
    Client -> Server: one line of plain text, e.g. "LIST_CATS", "GET_CAT 123",
                       "SET_STAT 123 str 5", "SET_HP 123 40"
    Server -> Client: one line of JSON, e.g. {"ok": true, "cats": [...]}

One pipe connection per request (the mod disconnects after each response),
so this file just opens/writes/reads/closes each call. Good enough for an
MVP -- if request latency matters later, consider keeping the pipe open
across calls (needs matching changes on the C++ side too, see docs/DEVELOPMENT.md).

Requires:
    pip install mcp pywin32
Run (only makes sense on Windows, with Mewgenics + the mod actually running):
    python server.py
"""

import json
import sys

import win32file
import win32pipe
import pywintypes

from mcp.server.fastmcp import FastMCP

import advisor
import breeding
import rooms as rooms_mod
from game_data import (EFFECT_NOTES, MUTATION_SLOTS, STAT_NAMES, enrich_cat, furniture_effects,
                       mutation_info, passive_levels, search_mutations, search_passives)

PIPE_NAME = r"\\.\pipe\cat_bridge"
PIPE_TIMEOUT_MS = 5000

mcp = FastMCP("mewgenics-cat-bridge")


def send_command(command: str) -> dict:
    """Open the named pipe, send one command line, read one JSON response line, close."""
    try:
        handle = win32file.CreateFile(
            PIPE_NAME,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0,
            None,
            win32file.OPEN_EXISTING,
            0,
            None,
        )
    except pywintypes.error as e:
        return {
            "ok": False,
            "error": f"could not connect to {PIPE_NAME}: {e}. "
                     f"Is Mewgenics running with cat_bridge.dll loaded?",
        }

    try:
        win32pipe.SetNamedPipeHandleState(handle, win32pipe.PIPE_READMODE_BYTE, None, None)
        win32file.WriteFile(handle, (command + "\n").encode("utf-8"))

        buf = b""
        while b"\n" not in buf:
            _, chunk = win32file.ReadFile(handle, 4096)
            if not chunk:
                break
            buf += chunk

        line = buf.split(b"\n", 1)[0]
        return json.loads(line.decode("utf-8"))
    except pywintypes.error as e:
        return {"ok": False, "error": f"pipe I/O error: {e}"}
    except json.JSONDecodeError as e:
        return {"ok": False, "error": f"malformed response from mod: {e}"}
    finally:
        win32file.CloseHandle(handle)


def send_cat_command(command: str) -> dict:
    """send_command, then add displayed stat totals to any cats in the response."""
    resp = send_command(command)
    if resp.get("ok"):
        for cat in resp.get("cats", []):
            enrich_cat(cat)
        if "cat" in resp:
            enrich_cat(resp["cat"])
    return resp


@mcp.tool()
def list_cats(include_all: bool = False) -> dict:
    """List the cats in the player's house in the running Mewgenics session,
    with their stats and equipped items.

    Each house cat has `room` (e.g. "Attic", "Floor1_Large"). The live scan
    also finds the daily stray waiting outside (outside=True, not yet adopted)
    and cats the game only loads for UI (e.g. the parents shown on a cat's
    stats panel, possibly long dead); both have in_house=False and are hidden
    unless include_all=True.

    `stats_base` is the heritable "DNA": when breeding, each of a kitten's
    base stats is copied from one parent's stats_base. Levelling, injury,
    class, item and passive bonuses are never inherited.

    `abilities` (names/descriptions in Russian as shown in-game, `name_en`
    too): `innate` = the active ability the cat was born with (what kittens
    inherit most often), `usable` = what it can use in battle (`learned`
    ones came from levels/runs), `basic_attack`. `passives` carry names and
    descriptions too (they also hold disorders like Pox).

    `stats` is the total the game displays: stats_base + stats_levelling +
    stats_injuries + stats_class (collar) + stats_items + stats_passives +
    stats_mutations (body-part mutations, listed in `mutations`). An hp of
    1073741823 (0x3FFFFFFF) is a game sentinel, apparently "not set / full"."""
    resp = send_cat_command("LIST_CATS")
    if resp.get("ok") and not include_all:
        resp["cats"] = [c for c in resp["cats"] if c["in_house"]]
    return resp


@mcp.tool()
def get_cat(sql_key: int) -> dict:
    """Get one cat's full stats/equipment by its sql_key (the id shown by list_cats)."""
    return send_cat_command(f"GET_CAT {sql_key}")


@mcp.tool()
def set_cat_stat(sql_key: int, stat: str, value: int) -> dict:
    """Set one of a cat's BASE stats (stats_base). stat must be one of:
    str, dex, con, int, spd, cha, lck.
    Base stats are heritable -- kittens bred from this cat can inherit the
    new value. Persists through the game's own save.
    The displayed total also includes levelling, injury, class and item
    bonuses, so to reach a displayed value X, set base to
    X - (stats[stat] - stats_base[stat])."""
    return send_cat_command(f"SET_STAT {sql_key} {stat} {value}")


@mcp.tool()
def set_cat_body_part(sql_key: int, part: str, sprite_index: int) -> dict:
    """Change one body part of a cat by sprite index -- this is how mutations
    are added or removed. The change is visible in-game immediately.

    part: texture, body, head, tail, mouth, leg1, leg2, arm1, arm2, lefteye,
    righteye, lefteyebrow, righteyebrow, leftear, rightear.
    sprite_index: >= 300 is a mutation from data/mutations/<file>.gon (arms
    and legs both use legs.gon, eyebrows use eyebrows.gon, ...); -2 removes
    the part (birth defect); lower values are normal, effect-free parts.
    A mutation counts once per slot: set BOTH ears for a symmetric look, the
    bonus is not doubled. Arms and legs are separate slots.
    The response's `previous` is the old index, to undo the change."""
    return send_cat_command(f"SET_PART {sql_key} {part} {sprite_index}")


@mcp.tool()
def find_mutations(query: str = "", slot: str | None = None, stat: str | None = None,
                   include_birth_defects: bool = False, limit: int = 30) -> dict:
    """Search body-part mutations in the game data. Mutations have no names
    of their own, only stats, a tag (animal, common, bird, extra, melted,
    birth_defect) and, for the ones with a special effect, a description:
    query matches the description (Russian or English) or the tag, e.g.
    "кровотечение", "regeneration", "bird".
    slot: texture (fur), body, head, tail, mouth, legs, arms, eyes,
    eyebrows, ears. stat (str, dex, con, int, spd, cha, lck): only
    mutations that raise it, best first. Birth defects are left out unless
    include_birth_defects. Returns slot + id to pass to add_cat_mutation."""
    if slot and slot not in MUTATION_SLOTS:
        return {"ok": False, "error": f"slot must be one of {list(MUTATION_SLOTS)}"}
    if stat and stat not in STAT_NAMES:
        return {"ok": False, "error": f"stat must be one of {list(STAT_NAMES)}"}
    return {"ok": True, "results": search_mutations(query, slot, stat, include_birth_defects, limit)}


@mcp.tool()
def add_cat_mutation(sql_key: int, slot: str, mutation_id: int, side: str = "both") -> dict:
    """Give a cat a body-part mutation (from find_mutations) by writing its
    id into the slot's body part(s) through set_cat_body_part; visible
    in-game immediately. Replaces whatever the slot had.
    slot: texture, body, head, tail, mouth, legs, arms, eyes, eyebrows,
    ears. side (paired slots only): both (default -- the bonus counts once
    per slot anyway, this just makes it look symmetric), left or right.
    The response's `previous` maps each changed part to its old sprite
    index: pass those to set_cat_body_part to undo."""
    if slot not in MUTATION_SLOTS:
        return {"ok": False, "error": f"slot must be one of {list(MUTATION_SLOTS)}"}
    info = mutation_info(slot, mutation_id)
    if info is None:
        return {"ok": False, "error": f"no mutation {mutation_id} in slot '{slot}' (use find_mutations)"}
    parts = MUTATION_SLOTS[slot][1]
    if len(parts) == 2 and side != "both":
        if side not in ("left", "right"):
            return {"ok": False, "error": "side must be both, left or right"}
        parts = (parts[0 if side == "left" else 1],)
    previous, resp = {}, {}
    for part in parts:
        resp = send_cat_command(f"SET_PART {sql_key} {part} {mutation_id}")
        if not resp.get("ok"):
            return dict(resp, previous=previous)
        previous[part] = resp["previous"]
    cat = resp.get("cat", {})
    return {"ok": True, "mutation": info, "previous": previous,
            "cat": {k: cat.get(k) for k in ("sql_key", "name", "mutations", "stats")}}


PASSIVE_SLOTS = ("passive1", "passive2", "disorder1", "disorder2")


@mcp.tool()
def find_passives(query: str = "", passive_class: str | None = None, limit: int = 30) -> dict:
    """Search passives and disorders in the game data by internal key or by
    name/description (Russian or English), e.g. "жаба", "Toad", "shield".
    passive_class narrows it down: Fighter, Hunter, Mage, Medic, Tank,
    Thief, Colorless, ..., or "Disorder" for disorders/diseases. Returns
    the key to use with set_cat_passive, its levels and stat bonuses."""
    return {"ok": True, "results": search_passives(query, passive_class, limit)}


@mcp.tool()
def set_cat_passive(sql_key: int, slot: str, passive_key: str, level: int = 1) -> dict:
    """Replace one of a cat's passives or disorders, like set_cat_stat does
    for stats. slot: passive1 / passive2 (the two passive slots) or
    disorder1 / disorder2 (disorders and diseases such as Pox). passive_key
    is the internal key from find_passives (or list_cats' passives[].key);
    "None" empties the slot. level must be one the passive defines (most
    class passives have 1 and 2). The response's `previous` /
    `previous_level` let you undo it. Persists through the game's own save.
    Passives from another class usually
    still work, but this isn't something the game normally allows."""
    if slot not in PASSIVE_SLOTS:
        return {"ok": False, "error": f"slot must be one of {PASSIVE_SLOTS}"}
    if passive_key != "None":
        levels = passive_levels(passive_key)
        if levels is None:
            return {"ok": False, "error": f"unknown passive '{passive_key}' (use find_passives)"}
        if level not in levels:
            return {"ok": False, "error": f"'{passive_key}' has levels {levels}"}
    if " " in passive_key:
        return {"ok": False, "error": "passive_key must be an internal key without spaces"}
    return send_cat_command(f"SET_PASSIVE {sql_key} {slot} {passive_key} {level}")


@mcp.tool()
def set_cat_hp(sql_key: int, value: int) -> dict:
    """Set a cat's current campaign HP."""
    return send_cat_command(f"SET_HP {sql_key} {value}")


# ---------------------------------------------------------------- breeding

def _load_breeding_state():
    """(Pedigree, {sql_key: enriched cat}) or an error dict."""
    ped_resp = send_command("PEDIGREE")
    if not ped_resp.get("ok"):
        return None, None, ped_resp
    cats_resp = send_cat_command("LIST_CATS")
    if not cats_resp.get("ok"):
        return None, None, cats_resp
    ped = breeding.Pedigree(ped_resp["pedigree"])
    return ped, {c["sql_key"]: c for c in cats_resp["cats"]}, None


def _load_rooms():
    """{room name: room} or an error dict."""
    resp = send_command("ROOMS")
    if not resp.get("ok"):
        return None, resp
    return {r["name"]: r for r in rooms_mod.load_rooms(resp)}, None


def _cat_ref(key, cats, ped):
    c = cats.get(key)
    ref = {"sql_key": key, "name": c["name"] if c else None, "coi": ped.coi(key)}
    if c:
        ref.update(sex=breeding.SEX_NAMES.get(c["sex"], c["sex"]), in_house=c["in_house"], dead=c["dead"])
    else:
        ref["note"] = "not loaded (not in the house right now: gone, dead elsewhere, or sold)"
    return ref


@mcp.tool()
def get_family(sql_key: int, generations: int = 3) -> dict:
    """Pedigree of one cat: ancestors up to `generations` back, children,
    siblings, and its coefficient of inbreeding (coi: 0 = no inbreeding,
    0.25 = child of full siblings or parent x child).

    Covers every cat the save has ever had, but names/stats are only known
    for cats currently loaded (in the house)."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    if sql_key not in ped.parents:
        return {"ok": False, "error": f"no pedigree record for cat {sql_key}"}
    sire, dam = ped.parents[sql_key]
    ancestors = ped.ancestors(sql_key, max_gen=generations)
    siblings = sorted({s for p in (sire, dam) if p is not None for s in ped.children.get(p, [])} - {sql_key})
    return {
        "ok": True,
        "cat": _cat_ref(sql_key, cats, ped),
        "sire": _cat_ref(sire, cats, ped) if sire is not None else None,
        "dam": _cat_ref(dam, cats, ped) if dam is not None else None,
        "ancestors": [dict(_cat_ref(a, cats, ped), generations_up=g, relation=ped.relation(sql_key, a))
                      for a, g in sorted(ancestors.items(), key=lambda kv: (kv[1], kv[0]))],
        "siblings": [dict(_cat_ref(s, cats, ped), relation=ped.relation(sql_key, s)) for s in siblings],
        "children": [_cat_ref(c, cats, ped) for c in sorted(ped.children.get(sql_key, []))],
    }



@mcp.tool()
def evaluate_pair(cat_a: int, cat_b: int, stimulation: float | None = None) -> dict:
    """Assess breeding two cats: can they breed at all (sex: sire = father,
    dam = mother, "?" cats can be either; gay cats only breed with "?" cats),
    how they're related, the kitten's coefficient of inbreeding (kitten_coi:
    0.25 = siblings/parent-child, 0.125 = half siblings, 0.0625 = cousins)
    and its risk tier, which base stats the kitten can inherit (each stat
    from one parent: `max` needs high Stimulation, `mean` = coin flip),
    which mutations each parent can pass on per body-part slot, love/hate,
    and the room they share (they only breed together if housed together;
    Comfort <= 0 means fights instead of kittens).

    Stimulation model (HYPOTHESIS from another project, not verified): per
    stat the kitten takes the better parent's value with p = 0.5 below 32
    Stimulation, 0.55 from 32, 0.7 from 95, 1.0 from 196. `stimulation`
    overrides the value used (default: the pair's shared room, else the
    house's best room) -- use it for what-ifs like "after buying furniture"."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    missing = [k for k in (cat_a, cat_b) if k not in cats]
    if missing:
        return {"ok": False, "error": f"cats not loaded (must be in the house): {missing}"}
    rooms, err = _load_rooms()
    return dict(breeding.evaluate_pair(ped, cats[cat_a], cats[cat_b], rooms, stimulation), ok=True)



@mcp.tool()
def suggest_breeding_pairs(stat_weights: dict[str, float] | None = None, max_kitten_coi: float = 0.0625,
                           top: int = 10, include_impossible: bool = False,
                           stimulation: float | None = None) -> dict:
    """Rank breeding pairs among the living cats in the house.

    stat_weights: how much each base stat matters, e.g. {"str": 2, "con": 1}
    (stats: str, dex, con, int, spd, cha, lck; omitted = 0). Default: all 1.
    Good mutations on a parent count too (half their weighted value, since
    the kitten gets each body-part slot from one parent).
    max_kitten_coi: skip pairs whose kitten would be more inbred than this
    (0 = only unrelated pairs, 0.0625 = up to cousins).
    Sorted by the expected kitten at the pair's Stimulation, then by the
    best possible kitten (each stat from the better parent).
    Pairs that can't breed (same sex, or a gay cat without a "?" partner)
    are skipped unless include_impossible=True. Each result says whether
    the two share a room -- they must, to breed. `stimulation` overrides the
    breeding room Stimulation used (see evaluate_pair for the model)."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    rooms, _ = _load_rooms()
    pool = _breeding_pool(cats)
    pairs = breeding.suggest_pairs(ped, pool, stat_weights, max_kitten_coi, top, include_impossible, rooms,
                                   stimulation)
    return {"ok": True, "pairs_considered_from": len(pool), "pairs": pairs,
            "strays_included": [c["name"] for c in pool if c["outside"]]}



@mcp.tool()
def plan_breeding(stat_weights: dict[str, float] | None = None, generations: int = 3,
                  max_kitten_coi: float = 0.0625, stimulation: float | None = None) -> dict:
    """Plan several generations of breeding towards 7s (the max base stat)
    in the stats you care about (stat_weights as in suggest_breeding_pairs;
    default all stats). Greedy: each generation picks the best pair, and
    from generation 2 on one parent is the previous planned kitten, so the
    line keeps improving without passing max_kitten_coi. Each step is the
    best case (every stat from the better parent) plus the chance of
    actually getting it and how many kittens that takes on average at the
    breeding room's Stimulation, with a what-if for higher Stimulation.
    Tells you which sex each planned kitten needs to be, and which stats no
    cat in the house can supply (bring in a stray that has them).
    `stimulation` defaults to the house's best room (see evaluate_pair for
    the Stimulation model, a hypothesis)."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    if stimulation is None:
        rooms, _ = _load_rooms()
        stimulation, _ = breeding.best_room_stimulation(rooms)
    return dict(breeding.plan_generations(ped, _breeding_pool(cats), stat_weights, generations, max_kitten_coi,
                                          stimulation=stimulation), ok=True)



@mcp.tool()
def evaluate_strays() -> dict:
    """The stray(s) waiting outside today: should you adopt them for
    breeding? Strays have no known parents, so they're unrelated to every
    house cat and dilute inbreeding. Reports which base stats they'd add
    that the house lacks or rarely has (7 is the max base stat), their best
    partners in the house, and a verdict."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    strays = [c for c in cats.values() if c["outside"]]
    if not strays:
        return {"ok": True, "strays": [], "note": "no stray waiting outside right now"}
    rooms, _ = _load_rooms()
    house = [c for c in cats.values() if c["in_house"]]
    return {"ok": True, "strays": advisor.evaluate_strays(ped, house, strays, rooms)}


@mcp.tool()
def breeding_roles() -> dict:
    """For every living house cat: keep it for breeding, or is it free to
    risk on adventures? Keepers carry rare base 7s, belong to the best
    pairs or the multi-generation plan, or have good mutations; cats that
    can't breed with anyone safely (e.g. gay without a '?' partner) or add
    nothing to the gene pool are free. Strays waiting outside count as
    potential partners."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    house = [c for c in cats.values() if c["in_house"]]
    strays = [c for c in cats.values() if c["outside"]]
    roles = advisor.breeding_assessment(ped, house, strays)
    return {"ok": True, "cats": [dict(roles[c["sql_key"]], sql_key=c["sql_key"], name=c["name"])
                                 for c in house if c["sql_key"] in roles]}


@mcp.tool()
def suggest_adventure_team(team_size: int = 4, classes: list[str] | None = None) -> dict:
    """Pick an adventure party from cats NOT needed for breeding (see
    breeding_roles), and a class (collar) for each. Adventures don't help
    breeding -- stats gained there aren't inherited -- so risk the cats the
    bloodline doesn't need. Fit = the cat's current total stats plus the
    class's stat_mods, over the stats that class levels up (from
    data/classes), plus half CON. Cats already wearing a collar keep it.
    classes: which collars are available (default the 6 base classes:
    Fighter, Hunter, Mage, Medic, Tank, Thief; add unlocked advanced ones
    like Monk, Butcher, Druid, Tinkerer, Necromancer, Psychic, Jester)."""
    ped, cats, err = _load_breeding_state()
    if err:
        return err
    house = [c for c in cats.values() if c["in_house"]]
    strays = [c for c in cats.values() if c["outside"]]
    unknown = [c for c in classes or [] if c not in advisor.class_table()]
    if unknown:
        return {"ok": False, "error": f"unknown classes {unknown}; known: {sorted(advisor.class_table())}"}
    roles = advisor.breeding_assessment(ped, house, strays)
    return dict(advisor.adventure_team(house, roles, team_size, tuple(classes or advisor.BASE_CLASSES)), ok=True)


def _breeding_pool(cats):
    """House cats plus the stray(s) waiting outside -- adopting a stray is
    the cheapest way to bring in unrelated genes."""
    return [c for c in cats.values() if c["in_house"] or c["outside"]]


# ---------------------------------------------------------------- rooms

@mcp.tool()
def get_rooms() -> dict:
    """The house's rooms: the stats the game shows (Comfort, Stimulation,
    Health, Evolution = the "Mutation" stat, Appeal, ...), how they come
    from the furniture (each piece's effects listed), the Comfort penalty for
    crowding (-1 per cat beyond 4), and which cats live in each room.
    `effect_notes` explains what each effect does for breeding."""
    rooms, err = _load_rooms()
    if err:
        return err
    cats_resp = send_command("LIST_CATS")
    names = {c["sql_key"]: c["name"] for c in cats_resp.get("cats", [])}
    out = []
    for r in rooms.values():
        out.append(dict(r, cats=[{"sql_key": k, "name": names.get(k)} for k in r["cats"]]))
    used = {k for info in furniture_effects().values() for k in info["effects"]}
    return {"ok": True, "rooms": out, "effect_notes": {k: v for k, v in EFFECT_NOTES.items() if k in used}}


@mcp.tool()
def suggest_room_setup(room: str, goal: str = "breeding") -> dict:
    """What furniture to move into / out of a room, and whether it's too
    crowded, for a goal:
      breeding   - Comfort + Stimulation + inheritance/fertility effects
      mutation   - Evolution (the Mutation stat): cats in the room mutate
      nursery    - Comfort + Health: raise kittens, keep good cats alive
      fight_club - low Comfort + Evolution: house fights give winners +1 stat
    Only considers furniture already placed in the house (not storage)."""
    rooms, err = _load_rooms()
    if err:
        return err
    if goal not in rooms_mod.GOALS:
        return {"ok": False, "error": f"goal must be one of {sorted(rooms_mod.GOALS)}"}
    if room not in rooms:
        return {"ok": False, "error": f"room must be one of {sorted(rooms)}"}
    return dict(rooms_mod.suggest_setup(list(rooms.values()), room, goal), ok=True)


if __name__ == "__main__":
    mcp.run(transport="stdio")
