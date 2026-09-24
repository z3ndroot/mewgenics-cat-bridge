"""
Who to keep for breeding, who to send on adventures, and whether to adopt
the daily stray -- built on breeding.py and game_data.py.

These are heuristics layered on verified mechanics (base-stat and body-part
inheritance, kinship/COI, the class stat_mods in data/classes/*.gon); the
cut-offs are ours, and each verdict comes with the reasons behind it so
the player (or Claude) can overrule it.
"""

from functools import lru_cache
from itertools import combinations, permutations

import breeding
from breeding import MAX_BASE_STAT, STAT_NAMES
from game_data import parse_gon, read_gpak_files

BASE_CLASSES = ("Fighter", "Hunter", "Mage", "Medic", "Tank", "Thief")
RARE_HOLDERS = 2          # a base 7 held by at most this many house cats is "rare"
SAFE_COI = 0.0625         # up to cousins


@lru_cache(maxsize=1)
def class_table():
    """{class: {"stat_mods": {...}, "key_stats": [...]}} from data/classes."""
    out = {}
    for name, raw in read_gpak_files(lambda n: n.startswith("data/classes/")).items():
        for cls, block in parse_gon(raw.decode("utf-8", "replace")).items():
            if not isinstance(block, dict) or cls == "Colorless":
                continue
            mods = {k: int(v) for k, v in (block.get("stat_mods") or {}).items()}
            out[cls] = {"stat_mods": mods, "key_stats": list(block.get("levelup_stats") or [])}
    return out


# ---------------------------------------------------------------- breeding value

def breeding_assessment(ped, house, strays=()):
    """Per living house cat: keep for breeding or free for adventures, with reasons."""
    living = [c for c in house if not c["dead"]]
    pool = living + list(strays)
    holders = {s: [c for c in living if c["stats_base"][s] >= MAX_BASE_STAT] for s in STAT_NAMES}
    top_pairs = breeding.suggest_pairs(ped, pool, max_kitten_coi=SAFE_COI, top=10)
    in_top = {}
    for p in top_pairs:
        for c in p["cats"]:
            in_top.setdefault(c["sql_key"], []).append(" x ".join(x["name"] for x in p["cats"]))
    plan = breeding.plan_generations(ped, pool, generations=3, max_kitten_coi=SAFE_COI)
    in_plan = {p["sql_key"] for st in plan["steps"] for p in st.get("parents", []) if p["sql_key"] is not None}

    out = {}
    for c in living:
        k = c["sql_key"]
        keep, free = [], []
        partners = [o for o in pool if o is not c and breeding.sire_dam(c, o) is not None
                    and ped.kinship(k, o["sql_key"]) <= SAFE_COI]
        if not partners:
            free.append("no partner it can breed with at a safe inbreeding level"
                        + (" (gay: needs a '?' cat)" if breeding.orientation(c) == "gay" and c["sex"] != 2 else ""))
        rare = [s for s in STAT_NAMES if c in holders[s] and len(holders[s]) <= RARE_HOLDERS]
        if rare:
            keep.append("carries a rare base 7 in " + ", ".join(rare) +
                        " (" + ", ".join(f"{s}: {len(holders[s])} cat(s)" for s in rare) + ")")
        if k in in_top:
            keep.append("in top breeding pairs: " + "; ".join(in_top[k][:3]))
        if k in in_plan:
            keep.append("part of the multi-generation breeding plan")
        mut = sum(v for m in c.get("mutations") or [] for v in m["stats"].values())
        if mut > 0:
            keep.append(f"has mutations worth {mut:+g} stats to pass on")
        elif mut < 0:
            free.append(f"its mutations are net {mut:+g} (would pass on defects)")
        if c.get("coi", 0) >= 0.25:
            free.append(f"heavily inbred itself (COI {c['coi']:.2f})")
        if not keep:
            free.append("nothing its offspring need that other cats don't already provide")
        out[k] = {"keep_for_breeding": bool(keep) and bool(partners), "why_keep": keep, "why_free": free}
    return out


# ---------------------------------------------------------------- adventures

def class_fit(cat, cls):
    """How well a cat suits a class: its current total stats (bonuses count
    on adventures) plus the collar's stat_mods, summed over the class's
    level-up stats, plus half its CON for survivability."""
    info = class_table()[cls]
    stats = dict(cat["stats"])
    if cat.get("collar") in (None, "", "Colorless"):
        for s, v in info["stat_mods"].items():
            stats[s] = stats.get(s, 0) + v
    key = info["key_stats"] if len(info["key_stats"]) < len(STAT_NAMES) else STAT_NAMES
    return sum(stats[s] for s in key) + 0.5 * stats["con"], stats


def adventure_team(house, assessment, team_size=4, classes=BASE_CLASSES):
    classes = [c for c in classes if c in class_table()]
    living = [c for c in house if not c["dead"]]
    free = [c for c in living if not assessment[c["sql_key"]]["keep_for_breeding"]]
    candidates = sorted(free, key=lambda c: -sum(c["stats"].values()))[:10]
    note = None
    if len(candidates) < team_size:
        spare = sorted((c for c in living if c not in candidates),
                       key=lambda c: len(assessment[c["sql_key"]]["why_keep"]))
        note = (f"only {len(candidates)} cats are free of breeding duties; filled with the least "
                f"important breeders -- consider bringing in strays first")
        candidates += spare[:team_size - len(candidates)]

    def options(cat):
        fixed = cat.get("collar") not in (None, "", "Colorless")
        return [cat["collar"]] if fixed and cat["collar"] in class_table() else classes

    fit = {(c["sql_key"], cls): class_fit(c, cls)[0] for c in candidates for cls in options(c)}
    best = None
    for team in combinations(candidates, min(team_size, len(candidates))):
        for assign in _assignments(team, options):
            score = sum(fit[(c["sql_key"], cls)] for c, cls in zip(team, assign))
            score -= 3 * (len(assign) - len(set(assign)))  # prefer a mixed party
            if best is None or score > best[0]:
                best = (score, team, assign)
    members = []
    for c, cls in zip(best[1], best[2]):
        fit, stats = class_fit(c, cls)
        members.append({
            "sql_key": c["sql_key"], "name": c["name"], "class": cls,
            "collar_already": c.get("collar") not in (None, "", "Colorless"),
            "stats_with_class": stats, "class_key_stats": class_table()[cls]["key_stats"],
            "fit_score": round(fit, 1),
            "abilities": [{"name": a["name"], "desc": a["desc"], "class": a["class"], "mana": a.get("mana")}
                          for a in (c.get("abilities") or {}).get("usable", [])],
            "passives": [{"name": p["name"], "desc": p.get("desc")} for p in c.get("passives") or []],
            "why_free_for_adventure": assessment[c["sql_key"]]["why_free"],
        })
    return {"team": members, "note": note,
            "bench": [{"sql_key": c["sql_key"], "name": c["name"]} for c in candidates if c not in best[1]]}


def _assignments(team, options):
    lists = [options(c) for c in team]
    # small search: try distinct classes first, then allow repeats if needed
    flat = sorted({cls for l in lists for cls in l})
    seen = False
    for perm in permutations(flat, len(team)):
        if all(cls in l for cls, l in zip(perm, lists)):
            seen = True
            yield perm
    if not seen:
        def rec(i, acc):
            if i == len(lists):
                yield tuple(acc)
                return
            for cls in lists[i]:
                yield from rec(i + 1, acc + [cls])
        yield from rec(0, [])


# ---------------------------------------------------------------- strays

def evaluate_strays(ped, house, strays, rooms_by_name=None):
    living = [c for c in house if not c["dead"]]
    house_max = {s: max(c["stats_base"][s] for c in living) for s in STAT_NAMES}
    holders = {s: sum(1 for c in living if c["stats_base"][s] >= MAX_BASE_STAT) for s in STAT_NAMES}
    best_house = breeding.suggest_pairs(ped, living, max_kitten_coi=SAFE_COI, top=1)
    best_house_score = best_house[0]["score_best"] if best_house else 0
    out = []
    for s in strays:
        brings = []
        for st in STAT_NAMES:
            v = s["stats_base"][st]
            if v > house_max[st]:
                brings.append(f"{st} {v} (house best is {house_max[st]})")
            elif v >= MAX_BASE_STAT and holders[st] <= RARE_HOLDERS:
                brings.append(f"{st} 7 (only {holders[st]} house cat(s) have it)")
        mut = sum(v for m in s.get("mutations") or [] for v in m["stats"].values())
        pairs = [breeding.evaluate_pair(ped, s, c, rooms_by_name) for c in living]
        pairs = [p for p in pairs if p["can_breed"]]
        pairs.sort(key=lambda p: (-p["kitten_best_total"], p["kitten_coi"]))
        top = pairs[:3]
        beats = bool(top) and top[0]["kitten_best_total"] > best_house_score
        verdict = "adopt" if brings or beats or mut > 0 else "optional"
        if not pairs:
            verdict = "adopt only for adventures (can't breed with anyone in the house)"
        out.append({
            "sql_key": s["sql_key"], "name": s["name"], "sex": breeding.SEX_NAMES.get(s["sex"]),
            "orientation": breeding.orientation(s),
            "stats_base": s["stats_base"], "base_total": sum(s["stats_base"].values()),
            "mutations": s.get("mutations"),
            "brings_new_genes": brings,
            "unrelated_to_house": all(ped.kinship(s["sql_key"], c["sql_key"]) == 0 for c in living),
            "best_partners": [{"partner": next(c["name"] for c in p["cats"] if c["sql_key"] != s["sql_key"]),
                               "kitten_best_total": p["kitten_best_total"],
                               "kitten_coi": p["kitten_coi"]} for p in top],
            "best_house_only_pair_total": best_house_score,
            "verdict": verdict,
        })
    return out
