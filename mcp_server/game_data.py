"""
Static game data read from Mewgenics' resources.gpak -- used to turn the raw
stat components the DLL reports into the totals the game actually displays.

Verified in-game (2026-09-24, v1.1.21239): displayed stat =
    stats_base + stats_levelling + stats_injuries + collar class stat_mods
    + passive stats (per level, e.g. ToadStyle lvl 1 = spd 4)
    + body-part mutations (data/mutations/*.gon, keyed by part sprite
      index; rules verified by editing sprite indices live, see
      cat_mutations())
Item stat bonuses (top-level `str 1` etc. in data/items/*.gon) are applied
too, but that part hasn't been checked against the game yet.

The game folder is found automatically through Steam; set MEWGENICS_DIR
(or MEWGENICS_GPAK for the archive itself) to override.

gpak format (reverse-engineered):
    u32 count
    count x { u16 name_len; char name[name_len]; u32 size }
    file data, concatenated in table order
"""

import csv
import io
import os
import re
import struct
from functools import lru_cache
from pathlib import Path


def find_game_dir():
    """Mewgenics install folder: $MEWGENICS_DIR, else every Steam library
    listed in Steam's libraryfolders.vdf, else Steam's default location."""
    if os.environ.get("MEWGENICS_DIR"):
        return Path(os.environ["MEWGENICS_DIR"])
    libraries = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            steam = Path(winreg.QueryValueEx(key, "SteamPath")[0])
        libraries.append(steam)
        vdf = steam / "steamapps" / "libraryfolders.vdf"
        if vdf.exists():
            for m in re.finditer(r'"path"\s+"([^"]+)"', vdf.read_text(encoding="utf-8", errors="replace")):
                libraries.append(Path(m.group(1).replace("\\\\", "\\")))
    except OSError:
        pass
    libraries.append(Path(r"C:\Program Files (x86)\Steam"))
    for lib in libraries:
        candidate = lib / "steamapps" / "common" / "Mewgenics"
        if (candidate / "resources.gpak").exists():
            return candidate
    return libraries[-1] / "steamapps" / "common" / "Mewgenics"


GPAK_PATH = Path(os.environ["MEWGENICS_GPAK"]) if os.environ.get("MEWGENICS_GPAK") \
    else find_game_dir() / "resources.gpak"

STAT_NAMES = ("str", "dex", "con", "int", "spd", "cha", "lck")


# ---------------------------------------------------------------- gpak

def read_gpak_index(f):
    """Return [(name, offset, size)] for every entry in an open gpak file."""
    (count,) = struct.unpack("<I", f.read(4))
    entries = []
    for _ in range(count):
        (name_len,) = struct.unpack("<H", f.read(2))
        name = f.read(name_len).decode("utf-8")
        (size,) = struct.unpack("<I", f.read(4))
        entries.append((name, size))
    offset = f.tell()
    index = []
    for name, size in entries:
        index.append((name, offset, size))
        offset += size
    return index


def read_gpak_files(predicate, gpak_path=GPAK_PATH):
    """Return {name: bytes} for every gpak entry whose name satisfies predicate."""
    out = {}
    with open(gpak_path, "rb") as f:
        for name, offset, size in read_gpak_index(f):
            if predicate(name):
                f.seek(offset)
                out[name] = f.read(size)
    return out


# ---------------------------------------------------------------- GON

def _tokenize(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace() or c == ",":
            i += 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            i = text.find("*/", i + 2)
            i = n if i < 0 else i + 2
        elif c in "{}[]":
            yield c
            i += 1
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            yield text[i + 1:j]
            i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '{}[],"':
                if text.startswith("//", j):
                    break
                j += 1
            yield text[i:j]
            i = j


def parse_gon(text):
    """Parse GON text into nested dicts/lists/strings. Duplicate keys: last wins."""
    tokens = list(_tokenize(text))
    pos = 0

    def value():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "{":
            return obj("}")
        if tok == "[":
            items = []
            while tokens[pos] != "]":
                items.append(value())
            pos += 1
            return items
        return tok

    def obj(end):
        nonlocal pos
        result = {}
        while pos < len(tokens) and tokens[pos] != end:
            key = tokens[pos]
            pos += 1
            result[key] = value()
        pos += 1
        return result

    return obj(None)


def _to_int(v):
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def _stat_block(block):
    """Extract {stat: int} from a dict's direct stat keys."""
    if not isinstance(block, dict):
        return {}
    return {k: _to_int(block[k]) for k in STAT_NAMES if _to_int(block.get(k)) is not None}


# ---------------------------------------------------------------- lookups

# DLL body_parts key -> (slot, data/mutations/<file>.gon). A mutation counts
# once per slot: both ears with the same mutation = one bonus. Arms and legs
# are separate slots that share legs.gon. (Verified in-game 2026-09-24.)
BODY_PART_GROUPS = {
    "texture": ("texture", "texture"), "body": ("body", "body"), "head": ("head", "head"),
    "tail": ("tail", "tail"), "mouth": ("mouth", "mouth"),
    "leg1": ("legs", "legs"), "leg2": ("legs", "legs"),
    "arm1": ("arms", "legs"), "arm2": ("arms", "legs"),
    "lefteye": ("eyes", "eyes"), "righteye": ("eyes", "eyes"),
    "lefteyebrow": ("eyebrows", "eyebrows"), "righteyebrow": ("eyebrows", "eyebrows"),
    "leftear": ("ears", "ears"), "rightear": ("ears", "ears"),
}
MUTATION_MIN_ID = 300  # below this: normal parts, except -2 = missing part


@lru_cache(maxsize=1)
def _data():
    prefixes = ("data/classes/", "data/items/", "data/passives/", "data/mutations/")
    files = read_gpak_files(lambda n: n.endswith(".gon") and n.startswith(prefixes))
    classes, items, passives, mutations = {}, {}, {}, {}
    for name, raw in files.items():
        parsed = parse_gon(raw.decode("utf-8", errors="replace"))
        for key, block in parsed.items():
            if not isinstance(block, dict):
                continue
            if name.startswith("data/classes/"):
                classes[key] = _stat_block(block.get("stat_mods"))
            elif name.startswith("data/items/"):
                items[key] = _stat_block(block)
            elif name.startswith("data/mutations/"):
                # file wraps everything in one block named after the group
                mutations[key] = {int(k): v for k, v in block.items()
                                  if k.lstrip("-").isdigit() and isinstance(v, dict)}
            else:
                passives[key] = block
    return classes, items, passives, mutations


# ---------------------------------------------------------------- abilities

# Measured on 141 families (both parents on record) in a 207-cat save,
# 2026-09-25: where a newborn's innate active ability came from.
ABILITY_INHERITANCE_OBSERVED = {
    "random_new": 93 / 141,          # not on either parent
    "parent_innate": 45 / 141,       # a parent's own innate ability
    "parent_learned": 3 / 141,       # an ability a parent learned later (levels / runs)
    "note": "measured on one save; Stimulation at birth unknown, so its effect isn't separated out",
}


@lru_cache(maxsize=1)
def _texts():
    raw = read_gpak_files(lambda n: n == "data/text/combined.csv")["data/text/combined.csv"]
    rows = list(csv.reader(io.StringIO(raw.decode("utf-8-sig"))))
    hdr = rows[0]
    en, ru = hdr.index("en"), hdr.index("ru")
    return {r[0]: (r[en], r[ru] or r[en]) for r in rows[1:] if len(r) > ru}


def _clean(s):
    """Strip the game's inline markup: [img:int] -> INT, drop [s:.7]/[m:..]."""
    s = re.sub(r"\[img:([a-z_]+)\]", lambda m: m.group(1).upper(), s or "")
    s = re.sub(r"\[/?[a-z]+(:[^\]]*)?\]", "", s)
    return s.replace("\\n", " ").replace("\n", " ").strip()


def _text(key):
    en, ru = _texts().get(key, (key, key)) if key else ("", "")
    return _clean(en), _clean(ru)


@lru_cache(maxsize=1)
def _ability_table():
    out = {}
    for name, raw in read_gpak_files(lambda n: n.startswith("data/abilities/")).items():
        for key, block in parse_gon(raw.decode("utf-8", "replace")).items():
            if isinstance(block, dict):
                out[key] = block
    return out


@lru_cache(maxsize=1)
def _passive_table():
    out = {}
    for name, raw in read_gpak_files(lambda n: n.startswith("data/passives/")).items():
        for key, block in parse_gon(raw.decode("utf-8", "replace")).items():
            if isinstance(block, dict):
                out[key] = block
    return out


def ability_info(key):
    """Localized name/description, class and mana cost of an active ability."""
    block = _ability_table().get(key)
    if block is None:
        return {"key": key, "name": key, "name_en": key, "desc": "", "class": None}
    meta = block.get("meta") or {}
    name_en, name_ru = _text(meta.get("name"))
    desc_en, desc_ru = _text(meta.get("desc"))
    cost = block.get("cost") or {}
    return {"key": key, "name": name_ru, "name_en": name_en, "desc": desc_ru, "desc_en": desc_en,
            "class": meta.get("class", "Colorless"), "mana": cost.get("mana")}


def passive_levels(key):
    """Levels a passive defines ("1", "2" blocks), or [1] if it has none.
    None if the key doesn't exist."""
    block = _passive_table().get(key)
    if block is None:
        return None
    levels = sorted(int(k) for k, v in block.items() if k.isdigit() and isinstance(v, dict))
    return levels or [1]


def search_passives(query="", cls=None, limit=30):
    """Passives/disorders whose key or (en/ru) name/description contains
    `query`, optionally only one class ("Disorder" for disorders)."""
    q = query.lower()
    out = []
    for key, block in _passive_table().items():
        if cls and (block.get("class") or "").lower() != cls.lower():
            continue
        info = passive_info(key)
        hay = " ".join([key, info["name"], info["name_en"], info["desc"], info["desc_en"]]).lower()
        if q in hay:
            out.append(dict(info, levels=passive_levels(key),
                            stats_by_level={lvl: passive_stat_mods(key, lvl) for lvl in passive_levels(key)}))
            if len(out) >= limit:
                break
    return out


def passive_info(key, level=1):
    """Localized name/description of a passive or disorder at a level."""
    block = _passive_table().get(key)
    if block is None:
        return {"key": key, "name": key, "desc": "", "class": None, "level": level}
    lvl = block.get(str(level)) if isinstance(block.get(str(level)), dict) else {}
    name_en, name_ru = _text(block.get("name"))
    desc_en, desc_ru = _text(lvl.get("desc") or block.get("desc"))
    return {"key": key, "name": name_ru, "name_en": name_en, "desc": desc_ru, "desc_en": desc_en,
            "class": block.get("class"), "level": level}


# ---------------------------------------------------------------- furniture

FURNITURE_META_KEYS = {"name", "desc", "special", "can_be_rare", "removed", "set", "rarity", "tags"}

# Room stats shown in-game, and how the game derives the displayed value.
# Comfort: furniture total minus 1 per cat beyond the first four (verified
# on 3 rooms). Poop is furniture ("poop": Comfort -2, Health -2) so it's
# already in the furniture total.
ROOM_CAT_COMFORT_FREE = 4

# What the breeding-relevant effects do, from the game's own tips (NPC Tink)
# and data/furniture_effects_guide.gon. Numbers are furniture "power".
EFFECT_NOTES = {
    "Comfort": "high = cats breed more often; low = fights (sometimes to the death). -1 per cat beyond 4 in the room",
    "Stimulation": "high = kittens take the BETTER parent's base stat, and inherit mutations/abilities/passives more often",
    "Health": "high = injuries heal and diseases get cured, cats live longer; low = disease spreads",
    "Evolution": "the 'Mutation' room stat: chance for cats in the room to gain mutations",
    "Appeal": "house-wide: better daily strays",
    "InheritStatFavorBest": "kittens favour the better parent's base stat",
    "InheritStatFavorMom": "kittens favour the mother's base stats",
    "InheritStatFavorDad": "kittens favour the father's base stats",
    "InheritPieceFavorMutation": "kittens favour a parent's MUTATED body part",
    "InheritAbilityChance": "chance to inherit an active ability",
    "InheritSecondAbilityChance": "chance to inherit a second active ability",
    "InheritPassiveChance": "chance to inherit a passive",
    "InheritDisorderChance": "chance to inherit a disorder (bad)",
    "IncreaseRoomBreedChance": "cats in the room breed more",
    "DecreaseRoomBreedChance": "cats in the room breed less",
    "IncreasePartnerBreedChance": "lovers breed more",
    "IncreaseFertility": "chance a breeding produces kittens",
    "BreedSuppression": "suppresses breeding",
    "IncreaseRoomFightChance": "more fights",
    "DecreaseRoomFightChance": "fewer fights",
    "FightRisk": "fights are more dangerous",
    "IncreaseLifespan": "cats live longer",
}


@lru_cache(maxsize=1)
def furniture_effects():
    """{furniture type: {effect: value}} for every furniture definition."""
    raw = read_gpak_files(lambda n: n == "data/furniture_effects.gon")["data/furniture_effects.gon"]
    out = {}
    for key, block in parse_gon(raw.decode("utf-8", errors="replace")).items():
        if not isinstance(block, dict):
            continue
        effects = {}
        for k, v in block.items():
            if k in FURNITURE_META_KEYS or isinstance(v, (dict, list)):
                continue
            try:
                effects[k] = float(v)
            except ValueError:
                pass
        out[key] = {"effects": effects, "set": block.get("set"), "removed": block.get("removed") == "true"}
    return out


def class_stat_mods(collar):
    return _data()[0].get(collar, {})


def item_stat_mods(item):
    return _data()[1].get(item, {}) if item else {}


def passive_stat_mods(name, level):
    """Passives/disorders: optional top-level `stats {}` plus `<level> { stats {} }`."""
    block = _data()[2].get(name)
    if block is None:
        return {}
    total = dict(_stat_block(block.get("stats")))
    level_block = block.get(str(level))
    if isinstance(level_block, dict):
        for stat, v in _stat_block(level_block.get("stats")).items():
            total[stat] = total.get(stat, 0) + v
    return total


def cat_mutations(body_parts):
    """Return [(slot, id, block)] for each mutation on a cat, one per
    (slot, id) -- e.g. horns on both ears count once, hooves on arms and on
    legs count twice. Two DIFFERENT mutations in one slot (left/right ear)
    are both counted; that case hasn't been seen in-game yet.
    """
    table = _data()[3]
    seen = {}
    for part, idx in (body_parts or {}).items():
        mapping = BODY_PART_GROUPS.get(part)
        if mapping is None or not (idx >= MUTATION_MIN_ID or idx == -2):
            continue
        slot, gon_file = mapping
        block = table.get(gon_file, {}).get(idx)
        if block is not None:
            seen.setdefault((slot, idx), block)
    return [(slot, idx, block) for (slot, idx), block in seen.items()]


# Mutation slot -> (data/mutations/<file>.gon, DLL body_parts keys in the slot
# as (left, right) or a single part). Same slots as BODY_PART_GROUPS.
MUTATION_SLOTS = {
    "texture": ("texture", ("texture",)), "body": ("body", ("body",)), "head": ("head", ("head",)),
    "tail": ("tail", ("tail",)), "mouth": ("mouth", ("mouth",)),
    "legs": ("legs", ("leg1", "leg2")), "arms": ("legs", ("arm1", "arm2")),
    "eyes": ("eyes", ("lefteye", "righteye")),
    "eyebrows": ("eyebrows", ("lefteyebrow", "righteyebrow")),
    "ears": ("ears", ("leftear", "rightear")),
}


def mutation_info(slot, idx):
    """Stats, tag and localized description of one mutation in a slot, or
    None if the slot's .gon file has no such id."""
    gon_file = MUTATION_SLOTS[slot][0]
    block = _data()[3].get(gon_file, {}).get(idx)
    if block is None:
        return None
    desc_en, desc_ru = _text(block.get("desc")) if block.get("desc") else ("", "")
    tag = block.get("tag")
    other = {k: block[k] for k in ("shield", "divine_shield", "override_move") if k in block}
    return {"slot": slot, "id": idx, "tag": tag,
            "birth_defect": tag == "birth_defect" or idx == -2,
            "stats": _stat_block(block), **({"other": other} if other else {}),
            "desc": desc_ru, "desc_en": desc_en}


def search_mutations(query="", slot=None, stat=None, include_defects=False, limit=30):
    """Mutations whose description (en/ru) or tag contains `query`,
    optionally in one slot, optionally only those that raise `stat`.
    Sorted by the stat bonus when `stat` is given."""
    q = query.lower()
    out = []
    for s in ([slot] if slot else MUTATION_SLOTS):
        for idx in sorted(_data()[3].get(MUTATION_SLOTS[s][0], {})):
            if idx < MUTATION_MIN_ID and idx != -2:
                continue
            info = mutation_info(s, idx)
            if info["birth_defect"] and not include_defects:
                continue
            if stat and info["stats"].get(stat, 0) <= 0:
                continue
            hay = " ".join([info["tag"] or "", info["desc"], info["desc_en"]]).lower()
            if q in hay:
                out.append(info)
    if stat:
        out.sort(key=lambda m: -m["stats"].get(stat, 0))
    return out[:limit]


def _add_into(acc, mods):
    for stat, v in mods.items():
        acc[stat] = acc.get(stat, 0) + v


# How the cat info tab turns CatData doubles into labels. Thresholds read
# from Mewgenics.exe 1.1.21239 (the code that picks the HOUSE_CAT_INFO_*
# text keys, RVA ~0xe3ee0-0xe4940; strict comparisons, so exactly 0.3 is
# "mid"). Libido/aggression: < 0.3 low, > 0.7 high. Inbreeding (coi):
# > 0.1 / 0.25 / 0.5 / 0.8. Sexuality: < 0.1 straight, > 0.9 gay, else bi
# (shown only as an icon).
TEMPERAMENT_LEVELS = {
    "libido": ((0.3, "low"), (0.7, "high"), "mid"),
    "aggression": ((0.3, "low"), (0.7, "high"), "mid"),
}
TEMPERAMENT_TEXT = {
    ("libido", "low"): "HOUSE_CAT_INFO_LOWLIBIDO", ("libido", "mid"): "HOUSE_CAT_INFO_MIDLIBIDO",
    ("libido", "high"): "HOUSE_CAT_INFO_HIGHLIBIDO",
    ("aggression", "low"): "HOUSE_CAT_INFO_LOWAGGRO", ("aggression", "mid"): "HOUSE_CAT_INFO_MIDAGGRO",
    ("aggression", "high"): "HOUSE_CAT_INFO_HIGHAGGRO",
}
INBREEDING_TIERS = ((0.8, 4), (0.5, 3), (0.25, 2), (0.1, 1))  # coi > x -> HOUSE_CAT_INFO_INBRED<n>


def _label(key):
    try:
        return _text(key)
    except OSError:
        return key, key


def inbreeding_tier(coi):
    """The game's inbreeding tier 0-4 for a COI and its (en, ru) label."""
    tier = next((t for cut, t in INBREEDING_TIERS if coi > cut), 0)
    return tier, _label(f"HOUSE_CAT_INFO_INBRED{tier}")


def orientation_label(sexuality):
    return "straight" if sexuality < 0.1 else "gay" if sexuality > 0.9 else "bi"


def cat_temperament(cat):
    """Libido, aggression, inbreeding and orientation as the game labels them."""
    out = {}
    for field, (lo, hi, default) in TEMPERAMENT_LEVELS.items():
        v = cat.get(field)
        if v is None:
            continue
        level = lo[1] if v < lo[0] else hi[1] if v > hi[0] else default
        en, ru = _label(TEMPERAMENT_TEXT[(field, level)])
        out[field] = {"value": round(v, 3), "level": level, "label": ru, "label_en": en}
    if cat.get("coi") is not None:
        tier, (en, ru) = inbreeding_tier(cat["coi"])
        out["inbreeding"] = {"coi": round(cat["coi"], 4), "tier": tier, "label": ru, "label_en": en}
    if cat.get("sexuality") is not None:
        out["orientation"] = orientation_label(cat["sexuality"])
    return out


def enrich_cat(cat):
    """Add displayed `stats` (plus class/item/passive parts) to a cat dict from the DLL."""
    parts = [cat.get("stats_base", {}), cat.get("stats_levelling", {}), cat.get("stats_injuries", {})]
    try:
        cls = class_stat_mods(cat.get("collar", ""))
        items, passives, mutations = {}, {}, {}
        for item in (cat.get("equipment") or {}).values():
            _add_into(items, item_stat_mods(item))
        for p in cat.get("passives") or []:
            _add_into(passives, passive_stat_mods(p["name"], p["level"]))
        cat["mutations"] = []
        for group, idx, block in cat_mutations(cat.get("body_parts")):
            _add_into(mutations, _stat_block(block))
            cat["mutations"].append({"part": group, "id": idx, "tag": block.get("tag"),
                                     "stats": _stat_block(block)})
        cat["stats_class"] = cls
        cat["stats_items"] = items
        cat["stats_passives"] = passives
        cat["stats_mutations"] = mutations
        parts += [cls, items, passives, mutations]
        _describe_abilities(cat)
    except OSError as e:
        cat["stats_note"] = f"game data unavailable ({e}); stats exclude class/item/passive bonuses"
    cat["stats"] = {s: sum(p.get(s, 0) for p in parts) for s in STAT_NAMES}
    cat["temperament"] = cat_temperament(cat)
    return cat


def _describe_abilities(cat):
    """Add `abilities` (localized) from the DLL's raw `actives` keys, and
    names/descriptions to `passives` (which include disorders)."""
    actives = cat.get("actives")
    if actives:
        innate = actives.get("inherited", [])
        basic = actives.get("basic", [])
        cat["abilities"] = {
            "basic_attack": ability_info(basic[1]) if len(basic) > 1 else None,
            # what it was born with -- what kittens can inherit most often
            "innate": [ability_info(k) for k in innate],
            # usable in battle; beyond the innate one these were learned
            # (levels / runs) and are only rarely passed on
            "usable": [dict(ability_info(k), learned=k not in innate) for k in actives.get("accessible", [])],
        }
    for p in cat.get("passives") or []:
        p.update({k: v for k, v in passive_info(p["name"], p["level"]).items() if k not in ("key", "level")},
                 key=p["name"])
