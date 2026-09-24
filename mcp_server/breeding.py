"""
Pedigree / kinship / breeding helpers on top of the DLL's PEDIGREE command.

Facts this relies on (verified 2026-09-24, v1.1.21239):
* PEDIGREE rows are [sql_key, parent_a, parent_b, coi], read live from the
  CatDatabase component; -1 parents = unknown (strays). Identical to the
  save file's `pedigree` table.
* parent_a is the sire and parent_b the dam: sex 0 (male) only ever
  appears as parent_a, sex 1 (female) only as parent_b. Sex 2 is shown
  in-game as "?" and can act as either parent; it appears in both positions.
* coi is the standard coefficient of inbreeding = kinship(sire, dam); our
  kinship() reproduces the game's stored value for every cat.
* Kittens inherit each base stat (stats_base) from one parent, and each
  body-part slot (both ears, both eyes, ...) as a whole from one parent --
  so a parent's mutation can be passed on.
From the game's tips / a community guide, not verified by us:
* Stimulation in the room makes kittens take the better parent's stat and
  pass mutations/abilities more often; furniture effects can bias it too.
* Gay cats (high `sexuality`) only breed with "?" cats. The 0.5 cut-off is
  a guess: in the test save sexuality is either <= 0.10 or >= 0.93.
* Inbreeding raises the chance of birth defects / bad mutations / disorders.
"""

from game_data import ABILITY_INHERITANCE_OBSERVED

STAT_NAMES = ("str", "dex", "con", "int", "spd", "cha", "lck")
SEX_NAMES = {0: "male", 1: "female", 2: "either (?)"}
GAY_SEXUALITY = 0.5
MAX_BASE_STAT = 7

# HYPOTHESIS (from jph6366/mewgenics-mcp, see docs/BREEDING_RESEARCH.md; not
# verified against the game): chance that a kitten takes the BETTER
# parent's value, per stat, by Stimulation. Consistent with what we've
# seen at Stimulation 13 (kittens got the lower value about half the time).
STIMULATION_TIERS = ((196, 1.0), (95, 0.7), (32, 0.55), (0, 0.5))
MUTATION_REROLL_CHANCE = 0.2  # hypothesis: per body-part slot


def p_better_stat(stimulation):
    for threshold, p in STIMULATION_TIERS:
        if stimulation >= threshold:
            return p
    return 0.5


def next_stimulation_tier(stimulation):
    """(threshold, p) of the next tier up, or None at the top."""
    higher = [(t, p) for t, p in STIMULATION_TIERS if t > stimulation]
    return min(higher) if higher else None


def birth_defect_chance(coi):
    """HYPOTHESIS (same source): chance of an extra birth defect."""
    return 0.02 + 0.4 * max(coi - 0.2, 0.0)


class Pedigree:
    def __init__(self, rows):
        self.parents = {}
        self.stored_coi = {}
        for key, a, b, coi in rows:
            self.parents[key] = (a if a >= 0 else None, b if b >= 0 else None)
            self.stored_coi[key] = coi
        self.children = {}
        for key, (a, b) in self.parents.items():
            for p in (a, b):
                if p is not None:
                    self.children.setdefault(p, []).append(key)
        self._depth = {}
        self._kin = {}

    def add_virtual(self, key, sire, dam):
        """Add a hypothetical kitten (for planning). Existing memoized values
        stay valid: nothing already computed can involve a new cat."""
        self.parents[key] = (sire, dam)
        for p in (sire, dam):
            if p is not None:
                self.children.setdefault(p, []).append(key)

    # -- structure

    def depth(self, key):
        """Generations above this cat (0 for founders)."""
        if key in self._depth:
            return self._depth[key]
        a, b = self.parents.get(key, (None, None))
        d = 0 if a is None and b is None else 1 + max(self.depth(p) for p in (a, b) if p is not None)
        self._depth[key] = d
        return d

    def ancestors(self, key, max_gen=None):
        """{ancestor: min generations up} (parents = 1)."""
        out = {}
        frontier = [(key, 0)]
        while frontier:
            k, g = frontier.pop()
            if max_gen is not None and g >= max_gen:
                continue
            for p in self.parents.get(k, (None, None)):
                if p is not None and (p not in out or out[p] > g + 1):
                    out[p] = g + 1
                    frontier.append((p, g + 1))
        return out

    # -- kinship

    def kinship(self, x, y):
        """Coefficient of kinship: probability that a random allele from x and
        one from y are identical by descent. A kitten of x and y has
        COI = kinship(x, y)."""
        if x is None or y is None:
            return 0.0
        if (x, y) in self._kin:
            return self._kin[(x, y)]
        if x == y:
            a, b = self.parents.get(x, (None, None))
            k = 0.5 * (1.0 + self.kinship(a, b))
        else:
            # recurse through the younger one, which can't be an ancestor of the other
            if self.depth(x) < self.depth(y):
                x, y = y, x
            a, b = self.parents.get(x, (None, None))
            k = 0.5 * (self.kinship(a, y) + self.kinship(b, y))
        self._kin[(x, y)] = self._kin[(y, x)] = k
        return k

    def coi(self, key):
        a, b = self.parents.get(key, (None, None))
        return self.kinship(a, b)

    # -- relationship label

    def relation(self, x, y):
        """What y is to x, in plain words (closest link only; inbred cats can
        be related in several ways at once -- kinship() accounts for all)."""
        if x == y:
            return "same cat"
        ax, ay = self.ancestors(x), self.ancestors(y)
        if y in ax:
            return _generations("parent", ax[y])
        if x in ay:
            return _generations("child", ay[x])
        px, py = set(self.parents.get(x, ())) - {None}, set(self.parents.get(y, ())) - {None}
        shared = px & py
        if shared:
            return "full sibling" if len(shared) == 2 else "half sibling"
        common = set(ax) & set(ay)
        if not common:
            return "unrelated (no known common ancestor)"
        gx, gy = min((ax[c], ay[c]) for c in common)
        if gx == 1:
            return _generations("nibling (niece/nephew)", gy - 1)
        if gy == 1:
            return _generations("aunt/uncle", gx - 1)
        if gx == gy:
            return f"cousin (degree {gx - 1})"
        return f"cousin (degree {min(gx, gy) - 1}, {abs(gx - gy)}x removed)"


def _generations(base, g):
    if g == 1:
        return base
    return ("grand" if g == 2 else "great-" * (g - 2) + "grand") + base


def inbreeding_level(coi):
    """Rough risk tier (thresholds are ours; the COI itself is exact)."""
    if coi <= 0:
        return "none"
    if coi <= 0.07:
        return "low (cousin level) - generally safe"
    if coi < 0.2:
        return "elevated (half-sibling level) - some risk of defects"
    if coi < 0.35:
        return "moderate (sibling / parent-child level) - birth defects and bad mutations more likely"
    return "high (repeated inbreeding) - severe defects, stat penalties likely"


# ---------------------------------------------------------------- pairs

def orientation(cat):
    return "gay" if cat.get("sexuality", 0.0) > GAY_SEXUALITY else "straight"


def sire_dam(cat_x, cat_y):
    """Return (sire, dam) if the two can plausibly breed, else None.
    Male x female; a "?" cat (sex 2) takes whichever role is missing;
    gay cats only breed with "?" cats. "?" x "?" assumed to work."""
    sx, sy = cat_x["sex"], cat_y["sex"]
    for c, other in ((cat_x, cat_y), (cat_y, cat_x)):
        if orientation(c) == "gay" and c["sex"] != 2 and other["sex"] != 2:
            return None
    if sx == 0 and sy in (1, 2) or sx == 2 and sy == 1:
        return cat_x, cat_y
    if sy == 0 and sx in (1, 2) or sy == 2 and sx == 1:
        return cat_y, cat_x
    if sx == 2 and sy == 2:
        return cat_x, cat_y
    return None


def kitten_stat_range(cat_x, cat_y, stimulation=0.0):
    """Each kitten base stat comes from one parent's stats_base. `expected`
    uses p_better_stat(stimulation) (a hypothesis); `max` is the best case."""
    p = p_better_stat(stimulation)
    out = {}
    for s in STAT_NAMES:
        a, b = cat_x["stats_base"][s], cat_y["stats_base"][s]
        hi, lo = max(a, b), min(a, b)
        out[s] = {"min": lo, "max": hi, "mean": (a + b) / 2,
                  "p_max": 1.0 if hi == lo else p, "expected": p * hi + (1 - p) * lo}
    return out


def chance_of_best_kitten(stat_range, wanted=STAT_NAMES):
    """Chance the kitten gets the better value in every wanted stat."""
    out = 1.0
    for s in wanted:
        out *= stat_range[s]["p_max"]
    return out


def slot_mutations(cat):
    """{slot: {"id", "tag", "stats"}} for each mutated/missing body-part slot
    (needs the cat to have gone through game_data.enrich_cat)."""
    return {m["part"]: {"id": m["id"], "tag": m.get("tag"), "stats": m["stats"]} for m in cat.get("mutations") or []}


def kitten_mutations(cat_x, cat_y):
    """Per slot, what each parent would pass on. The kitten gets each slot
    from one parent (verified), Stimulation reportedly favours the mutated
    one, and each slot may be re-rolled at random (~20%, hypothesis)."""
    mx, my = slot_mutations(cat_x), slot_mutations(cat_y)
    out = []
    for slot in sorted(set(mx) | set(my)):
        out.append({
            "slot": slot,
            cat_x["name"]: mx.get(slot, "normal"),
            cat_y["name"]: my.get(slot, "normal"),
        })
    return out


def kitten_ability_outlook(cat_x, cat_y):
    """Which active abilities a kitten could inherit, with the shares
    observed in a real save (game_data.ABILITY_INHERITANCE_OBSERVED)."""
    innate, learned = [], []
    for c in (cat_x, cat_y):
        ab = c.get("abilities") or {}
        for a in ab.get("innate", []):
            innate.append({"from": c["name"], "key": a["key"], "name": a["name"], "desc": a.get("desc")})
        for a in ab.get("usable", []):
            if a.get("learned"):
                learned.append({"from": c["name"], "key": a["key"], "name": a["name"], "desc": a.get("desc")})
    obs = ABILITY_INHERITANCE_OBSERVED
    return {
        "parents_innate": innate,
        "parents_learned": learned,
        "observed_odds": {
            "one of the parents' innate abilities": round(obs["parent_innate"], 2),
            "one of the parents' learned abilities": round(obs["parent_learned"], 2),
            "a random new ability": round(obs["random_new"], 2),
        },
        "note": "odds measured on 141 families of one save (Stimulation at birth unknown); the game's tips say "
                "high Stimulation raises the chance of inheriting abilities",
    }


def evaluate_pair(ped, cat_x, cat_y, rooms_by_name=None, stimulation=None):
    """stimulation: the breeding room's Stimulation to assume. Default: the
    room the two share, else the best room in the house (move them there)."""
    kx, ky = cat_x["sql_key"], cat_y["sql_key"]
    roles = sire_dam(cat_x, cat_y)
    kitten_coi = ped.kinship(kx, ky)
    stim_source = "given"
    if stimulation is None:
        stimulation, stim_source = _pair_stimulation(cat_x, cat_y, rooms_by_name)
    stats = kitten_stat_range(cat_x, cat_y, stimulation)
    warnings = []
    if roles is None:
        why = [f"{c['name']} is gay (only breeds with '?' cats)" for c in (cat_x, cat_y)
               if orientation(c) == "gay" and c["sex"] != 2]
        warnings.append("can't produce a kitten: " + (
            "; ".join(why) if why else f"{SEX_NAMES.get(cat_x['sex'])} + {SEX_NAMES.get(cat_y['sex'])}"))
    if kitten_coi > 0:
        warnings.append(f"inbreeding: {inbreeding_level(kitten_coi)}")
    for c, other in ((cat_x, cat_y), (cat_y, cat_x)):
        if c.get("hater_sql_key") == other["sql_key"]:
            warnings.append(f"{c['name']} hates {other['name']} (fights, less likely to breed)")
        if c.get("dead"):
            warnings.append(f"{c['name']} is dead")
        if c.get("outside"):
            warnings.append(f"{c['name']} is today's stray: adopt it and house it with {other['name']}")
    room = None
    if cat_x.get("room") and cat_x.get("room") == cat_y.get("room"):
        room = cat_x["room"]
    elif cat_x.get("room") and cat_y.get("room"):
        warnings.append(f"not in the same room ({cat_x['room']} / {cat_y['room']}): they only breed together if housed together")
    room_info = None
    if room and rooms_by_name and room in rooms_by_name:
        r = rooms_by_name[room]
        room_info = {"name": room, "comfort": r["displayed"].get("Comfort", 0.0),
                     "stimulation": r["displayed"].get("Stimulation", 0.0), "cats": len(r["cats"])}
        if room_info["comfort"] <= 0:
            warnings.append(f"room comfort {room_info['comfort']:g}: breeding unlikely, fights likely")
    muts = kitten_mutations(cat_x, cat_y)
    p_best = chance_of_best_kitten(stats)
    return {
        "cats": [{"sql_key": c["sql_key"], "name": c["name"], "sex": SEX_NAMES.get(c["sex"], c["sex"]),
                  "orientation": orientation(c)} for c in (cat_x, cat_y)],
        "can_breed": roles is not None,
        "sire": roles[0]["sql_key"] if roles else None,
        "dam": roles[1]["sql_key"] if roles else None,
        "relation": ped.relation(kx, ky),
        "kitten_coi": kitten_coi,
        "inbreeding": inbreeding_level(kitten_coi),
        "birth_defect_chance_hypothesis": birth_defect_chance(kitten_coi),
        "stimulation": {"value": stimulation, "source": stim_source,
                        "p_better_stat_hypothesis": p_better_stat(stimulation),
                        "next_tier": _tier_ref(next_stimulation_tier(stimulation))},
        "kitten_stats_base": stats,
        "kitten_best_total": sum(v["max"] for v in stats.values()),
        "kitten_expected_total": round(sum(v["expected"] for v in stats.values()), 2),
        "chance_of_best_kitten": p_best,
        "kittens_needed_on_average_for_best": round(1 / p_best, 1) if p_best > 0 else None,
        "kitten_mutations_by_slot": muts,
        "kitten_active_ability": kitten_ability_outlook(cat_x, cat_y),
        "lovers": cat_x.get("lover_sql_key") == ky or cat_y.get("lover_sql_key") == kx,
        "room": room_info,
        "warnings": warnings,
    }


def _tier_ref(tier):
    return None if tier is None else {"stimulation": tier[0], "p_better_stat": tier[1]}


def best_room_stimulation(rooms_by_name):
    if not rooms_by_name:
        return 0.0, None
    name, room = max(rooms_by_name.items(), key=lambda kv: kv[1]["displayed"].get("Stimulation", 0.0))
    return room["displayed"].get("Stimulation", 0.0), name


def _pair_stimulation(cat_x, cat_y, rooms_by_name):
    room = cat_x.get("room")
    if room and room == cat_y.get("room") and rooms_by_name and room in rooms_by_name:
        return rooms_by_name[room]["displayed"].get("Stimulation", 0.0), f"their room {room}"
    value, name = best_room_stimulation(rooms_by_name)
    return value, (f"best room {name} (move them there)" if name else "unknown, assumed 0")


def _weights(stat_weights):
    if not stat_weights:
        return {s: 1.0 for s in STAT_NAMES}
    return {s: float(stat_weights.get(s, 0.0)) for s in STAT_NAMES}


def mutation_value(cat, weights):
    """Weighted stat value of a cat's mutations (what it could pass on)."""
    return sum(weights[s] * v for m in cat.get("mutations") or [] for s, v in m["stats"].items() if s in weights)


def suggest_pairs(ped, cats, stat_weights=None, max_kitten_coi=0.0625, top=10, include_impossible=False,
                  rooms_by_name=None, stimulation=None):
    """Ranked by the EXPECTED kitten at the pair's Stimulation (see
    evaluate_pair), then by the best case."""
    weights = _weights(stat_weights)
    pool = [c for c in cats if not c.get("dead")]
    results = []
    for i, x in enumerate(pool):
        for y in pool[i + 1:]:
            if sire_dam(x, y) is None and not include_impossible:
                continue
            kin = ped.kinship(x["sql_key"], y["sql_key"])
            if kin > max_kitten_coi:
                continue
            stim = stimulation if stimulation is not None else _pair_stimulation(x, y, rooms_by_name)[0]
            rng = kitten_stat_range(x, y, stim)
            best = sum(weights[s] * rng[s]["max"] for s in STAT_NAMES)
            expected = sum(weights[s] * rng[s]["expected"] for s in STAT_NAMES)
            # a good mutation on either parent can be passed on (roughly half the time)
            muts = 0.5 * (max(mutation_value(x, weights), 0) + max(mutation_value(y, weights), 0))
            results.append((expected + muts, best + muts, -kin, x, y))
    results.sort(key=lambda r: (r[0], r[1], r[2]), reverse=True)
    return [dict(evaluate_pair(ped, x, y, rooms_by_name, stimulation),
                 score_expected=round(expected, 2), score_best=best)
            for expected, best, _, x, y in results[:top]]


# ---------------------------------------------------------------- planning

def plan_generations(ped, cats, stat_weights=None, generations=3, max_kitten_coi=0.0625, target=MAX_BASE_STAT,
                     stimulation=0.0):
    """Greedy multi-generation plan towards `target` in the weighted stats.

    Each generation picks the pair whose best-case kitten (every stat from
    the better parent) scores best, then adds that hypothetical kitten to
    the pool so later generations can use it. The kitten's sex is unknown
    in advance, so it's treated as able to pair with anyone of a compatible
    sex -- the plan says which sex it needs. Each step also gives the chance
    of actually getting that kitten at `stimulation` (hypothesis tiers), and
    a what-if for higher Stimulation.
    """
    weights = _weights(stat_weights)
    wanted = [s for s in STAT_NAMES if weights[s] > 0]
    pool = [dict(c) for c in cats if not c.get("dead")]
    steps = []
    for gen in range(1, generations + 1):
        best = None
        for i, x in enumerate(pool):
            for y in pool[i + 1:]:
                if not (x.get("virtual") or y.get("virtual")) and gen > 1 and steps:
                    # after gen 1, build on the line: one parent must be a planned kitten
                    continue
                roles = _plan_roles(x, y)
                if roles is None:
                    continue
                kin = ped.kinship(x["sql_key"], y["sql_key"])
                if kin > max_kitten_coi:
                    continue
                kitten = {s: max(x["stats_base"][s], y["stats_base"][s]) for s in STAT_NAMES}
                score = sum(weights[s] * kitten[s] for s in STAT_NAMES)
                key = (score, -kin)
                if best is None or key > best[0]:
                    best = (key, x, y, kitten, kin, roles)
        if best is None:
            steps.append({"generation": gen, "note": "no compatible pair within the inbreeding limit"})
            break
        (score, _), x, y, kitten, kin, roles = best
        vkey = -gen
        ped.add_virtual(vkey, x["sql_key"], y["sql_key"])
        name = f"planned kitten G{gen}"
        pool.append({"sql_key": vkey, "name": name, "virtual": True, "sex": 2, "sexuality": 0.0,
                     "stats_base": kitten, "dead": False})
        reached = all(kitten[s] >= target for s in wanted)
        p = chance_of_best_kitten(kitten_stat_range(x, y, stimulation), wanted)
        steps.append({
            "generation": gen,
            "parents": [_plan_ref(x), _plan_ref(y)],
            "parent_roles": roles,
            "kitten_coi": kin,
            "inbreeding": inbreeding_level(kin),
            "kitten_stats_base_if_best": kitten,
            "weighted_score": score,
            "chance_per_kitten": p,
            "kittens_needed_on_average": round(1 / p, 1),
            "target_reached": reached,
            "_pair": (x, y),
        })
        if reached:
            break
    missing = {}
    if steps and "kitten_stats_base_if_best" in steps[-1]:
        last = steps[-1]["kitten_stats_base_if_best"]
        missing = {s: last[s] for s in wanted if last[s] < target}

    def total_kittens(stim):
        return round(sum(1 / chance_of_best_kitten(kitten_stat_range(*st["_pair"], stim), wanted)
                         for st in steps if "_pair" in st), 1)

    what_if = {f"stimulation {t}": {"p_better_stat": p, "kittens_needed_total": total_kittens(t)}
               for t, p in sorted(STIMULATION_TIERS) if t > stimulation}
    needed_now = total_kittens(stimulation)
    for st in steps:
        st.pop("_pair", None)
    return {
        "stimulation": stimulation,
        "p_better_stat_hypothesis": p_better_stat(stimulation),
        "kittens_needed_total": needed_now,
        "what_if_more_stimulation": what_if,
        "notes": [
            "kittens_needed counts only the stat lottery (tiers are a hypothesis); each planned kitten "
            "must ALSO be the right sex for the next step, roughly doubling the count",
            "raising the breeding room's Stimulation (furniture) is the main lever: see suggest_room_setup",
        ],
        "steps": steps,
        "still_below_target": missing,
        "hint": ("no cat in the house carries a " + str(target) + " in " + ", ".join(missing) +
                 ": bring in a stray/cat that has it, or a mutation that raises it") if missing else None,
    }


def _plan_roles(x, y):
    """Like sire_dam, but a planned kitten (virtual) can be born either sex."""
    if x.get("virtual") or y.get("virtual"):
        real = y if x.get("virtual") else x
        if x.get("virtual") and y.get("virtual"):
            return "both planned kittens: need one male and one female (or a '?')"
        if orientation(real) == "gay" and real["sex"] != 2:
            return f"planned kitten must be a '?' (since {real['name']} is gay)"
        need = {0: "female or '?'", 1: "male or '?'", 2: "any sex"}[real["sex"]]
        return f"planned kitten must be {need} to pair with {real['name']}"
    r = sire_dam(x, y)
    return None if r is None else f"sire {r[0]['name']}, dam {r[1]['name']}"


def _plan_ref(c):
    return {"sql_key": c["sql_key"] if not c.get("virtual") else None, "name": c["name"],
            "stats_base": c["stats_base"]}
