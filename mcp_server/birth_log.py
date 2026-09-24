"""
Birth log: records every night in the house so the breeding hypotheses can
be checked against what the game actually does.

A background poller (started by server.py, or tools/birth_logger.py on its
own) takes a snapshot every POLL_SECONDS: the day counter (MewDirector +
0x580, the save's current_day), the house cats, the rooms and the pedigree.
When the day goes up it writes one "night" record built from the last
snapshot of the previous day (who lived where, room Comfort / Stimulation,
the predicted mating chance of every co-housed pair) plus the kittens that
appeared in the pedigree overnight (parents, base stats, body parts,
innate abilities). One JSON object per line in LOG_PATH.

analyze() compares the records with:
* the Stimulation hypothesis (breeding.STIMULATION_TIERS: how often a
  kitten takes the better parent's base stat),
* the mating model read from the game's code (breeding.mating_outlook:
  chance that a co-housed pair has kittens, litter size from fertility).
"""

import hashlib
import json
import os
import threading
import time
from pathlib import Path

import breeding

LOG_PATH = Path(os.environ.get("MEWGENICS_BIRTH_LOG")
                or Path(os.environ.get("LOCALAPPDATA", Path.home())) / "mewgenics-cat-bridge" / "birth_log.jsonl")
POLL_SECONDS = 10

CAT_FIELDS = ("sql_key", "name", "sex", "room", "stats_base", "libido", "sexuality", "fertility", "aggression",
              "lover_sql_key", "lover_affinity", "hater_sql_key", "hater_affinity", "coi", "body_parts")


def save_fingerprint(pedigree_rows):
    """Identifies the save slot: hash of its oldest pedigree entries."""
    oldest = sorted((int(k), int(a), int(b)) for k, a, b, _ in pedigree_rows)[:32]
    return hashlib.sha1(json.dumps(oldest).encode()).hexdigest()[:12]


def _compact_cat(c):
    out = {k: c.get(k) for k in CAT_FIELDS}
    out["cha"] = (c.get("stats") or {}).get("cha")
    out["innate"] = [a["key"] for a in (c.get("abilities") or {}).get("innate", [])]
    out["mutations"] = [[m["part"], m["id"]] for m in c.get("mutations") or []]
    return out


def _room_stat(room, name):
    return room["displayed"].get(name, 0.0)


class Snapshot:
    def __init__(self, day, pedigree_rows, cats, rooms_by_name):
        self.time = time.time()
        self.day = day
        self.save = save_fingerprint(pedigree_rows)
        self.parents = {k: (a, b) for k, a, b, _ in pedigree_rows}
        self.cats = {c["sql_key"]: c for c in cats if c.get("in_house")}
        self.rooms = rooms_by_name

    @property
    def usable(self):
        return self.day is not None and self.day >= 0 and bool(self.rooms) and bool(self.cats)


def night_record(before, after, kitten_cache=None):
    """The record for the night between two snapshots of consecutive days.
    kitten_cache: {key: compact cat} of kittens seen since `before`, for
    kittens the player removed before `after` was taken."""
    rooms = {name: {"Comfort": _room_stat(r, "Comfort"), "Stimulation": _room_stat(r, "Stimulation"),
                    "cats": [c["sql_key"] if isinstance(c, dict) else c for c in r["cats"]]}
             for name, r in before.rooms.items()}
    stims = [r["Stimulation"] for r in rooms.values()]
    pairs = []
    by_room = {}
    for c in before.cats.values():
        by_room.setdefault(c.get("room"), []).append(c)
    for room, members in by_room.items():
        if room not in before.rooms:
            continue
        comfort = rooms[room]["Comfort"]
        for i, x in enumerate(members):
            for y in members[i + 1:]:
                m = breeding.mating_outlook(x, y, comfort)
                pairs.append({"a": x["sql_key"], "b": y["sql_key"], "room": room,
                              "compatible": breeding.sire_dam(x, y) is not None,
                              "attraction": [m["attraction"][x["name"]], m["attraction"][y["name"]]],
                              "chance_both_agree": m["chance_both_agree"],
                              "expected_kittens_per_mating": m["expected_kittens_per_mating"]})
    births = []
    for key in sorted(set(after.parents) - set(before.parents)):
        sire, dam = after.parents[key]
        if sire < 0 and dam < 0:
            continue  # a stray, not a birth
        kitten = after.cats.get(key)
        room = next((before.cats[p].get("room") for p in (sire, dam) if p in before.cats), None)
        births.append({"kitten": key, "sire": sire, "dam": dam, "parents_room": room,
                       "kitten_cat": _compact_cat(kitten) if kitten else (kitten_cache or {}).get(key)})
    return {
        "type": "night", "save": after.save, "day_before": before.day, "day_after": after.day,
        "logged_at": round(after.time), "snapshot_age_s": round(after.time - before.time),
        "rooms": rooms, "house_stimulation": {"best": max(stims, default=0), "total": sum(stims)},
        "cats": {str(k): _compact_cat(c) for k, c in before.cats.items()},
        "pairs": pairs, "births": births,
    }


def read_log(path=LOG_PATH):
    if not path.exists():
        return []
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    return out


class BirthLogger:
    """Feed it snapshots with update(); it appends night records.

    Doesn't assume whether the game adds the kittens before or after it
    bumps the day counter: `pre` is the last snapshot taken before any new
    kitten showed up (the state the night was decided on), and a night is
    written one poll after the day changed, so late kittens are included.
    """

    def __init__(self, path=LOG_PATH):
        self.path = path
        self.pre = None        # last snapshot with no births since `baseline`
        self.baseline = set()  # pedigree keys known when the last night was closed
        self.pending = None    # first snapshot of the new day, waiting one more poll
        self.last_error = None
        self.last_poll = None
        self.last_day = None
        self.nights_written = 0
        self.events = []       # recent state changes, for diagnosing missed nights
        self.kittens = {}      # compact data of new kittens, kept in case they're removed

    def _event(self, snap, what):
        self.events = (self.events + [{"t": round(snap.time), "day": snap.day, "what": what}])[-20:]
        if self.on_event:
            self.on_event(self.events[-1])

    on_event = None

    def _logged(self, save, day_after):
        return any(r.get("save") == save and r.get("day_after") == day_after for r in read_log(self.path))

    def _reset(self, snap):
        self.pre, self.pending = snap, None
        self.baseline = set(snap.parents)
        self.kittens = {}

    def update(self, snap):
        self.last_poll = snap.time
        if not snap.usable:
            if self.pre is not None and snap.day != self.pre.day:
                self._event(snap, f"unusable snapshot (rooms={len(snap.rooms or {})}, cats={len(snap.cats)})")
            return None
        self.last_day = snap.day
        if self.pre is None or self.pre.save != snap.save or snap.day < self.pre.day:
            why = ("start" if self.pre is None else "other save" if self.pre.save != snap.save
                   else f"day went back {self.pre.day} -> {snap.day}")
            self._event(snap, f"reset: {why}")
            self._reset(snap)  # first snapshot, another save slot, or an older save loaded
            return None
        born = any(k not in self.baseline and (a >= 0 or b >= 0) for k, (a, b) in snap.parents.items())
        for k, c in snap.cats.items():
            if k not in self.baseline and k not in self.kittens:
                self.kittens[k] = _compact_cat(c)
        if snap.day == self.pre.day:
            if born and self.pre is not None and not getattr(self, "_born_seen", False):
                self._event(snap, "kittens appeared before the day changed")
            self._born_seen = born
            if not born:
                self.pre = snap
                self.baseline |= set(snap.parents)  # strays arriving during the day
            return None
        if self.pending is None or self.pending.day != snap.day:
            self.pending = snap  # the day just changed: give late kittens one more poll
            self._event(snap, f"day changed {self.pre.day} -> {snap.day}")
            return None
        record = night_record(self.pre, snap, self.kittens)
        if snap.day - self.pre.day != 1:
            record["gap"] = True   # we missed a day: don't use it for per-night statistics
        self._reset(snap)
        self._born_seen = False
        if self._logged(record["save"], record["day_after"]):
            self._event(snap, "night already logged")
            return None
        self._event(snap, f"night logged: {len(record['births'])} birth(s)")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "a", encoding="utf-8") as f:
            f.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.nights_written += 1
        return record

    def status(self):
        return {"log_path": str(self.path), "last_poll": self.last_poll, "current_day": self.last_day,
                "waiting_to_close_night": self.pending is not None,
                "nights_written_this_session": self.nights_written, "last_error": self.last_error,
                "recent_events": self.events[-5:]}


def run_poller(fetch, logger, stop=None, interval=POLL_SECONDS, on_record=None):
    """fetch() -> Snapshot or None (game not reachable)."""
    while stop is None or not stop.is_set():
        try:
            snap = fetch()
            if snap is not None:
                rec = logger.update(snap)
                logger.last_error = None
                if rec and on_record:
                    on_record(rec)
        except Exception as e:  # keep polling whatever happens
            logger.last_error = f"{type(e).__name__}: {e}"
            if logger.on_event:
                logger.on_event({"t": round(time.time()), "what": f"error: {logger.last_error}"})
        if stop is not None:
            stop.wait(interval)
        else:
            time.sleep(interval)


def start_background(fetch, logger):
    stop = threading.Event()
    t = threading.Thread(target=run_poller, args=(fetch, logger, stop), name="birth-log", daemon=True)
    t.start()
    return stop


# ---------------------------------------------------------------- analysis

def _bin(p, width=0.1):
    lo = min(int(p / width), int(1 / width) - 1) * width
    return f"{lo:.1f}-{lo + width:.1f}"


def analyze(records):
    nights = [r for r in records if r.get("type") == "night"]
    clean = [r for r in nights if not r.get("gap")]
    births = [(r, b) for r in nights for b in r["births"]]

    # 1. Stimulation: per stat where the parents differ, did the kitten take the better value?
    by_tier = {}
    for r, b in births:
        cats = r["cats"]
        sire, dam, kit = cats.get(str(b["sire"])), cats.get(str(b["dam"])), b.get("kitten_cat")
        if not (sire and dam and kit and b.get("parents_room") in r["rooms"]):
            continue
        stim = r["rooms"][b["parents_room"]]["Stimulation"]
        tier = max(t for t, _ in breeding.STIMULATION_TIERS if stim >= t) if stim >= 0 else 0
        entry = by_tier.setdefault(tier, {"better": 0, "worse": 0, "neither": 0, "stimulation_values": set()})
        entry["stimulation_values"].add(stim)
        for s in breeding.STAT_NAMES:
            a, d, k = sire["stats_base"][s], dam["stats_base"][s], kit["stats_base"][s]
            if a == d:
                continue
            entry["better" if k == max(a, d) else "worse" if k == min(a, d) else "neither"] += 1
    stim_result = []
    for tier, e in sorted(by_tier.items()):
        n = e["better"] + e["worse"]
        stim_result.append({"stimulation_tier_from": tier, "room_stimulation_seen": sorted(e["stimulation_values"]),
                            "stats_compared": n, "took_better": e["better"], "took_worse": e["worse"],
                            "neither_parent": e["neither"],
                            "observed_p_better": round(e["better"] / n, 3) if n else None,
                            "hypothesis_p_better": breeding.p_better_stat(tier)})

    # 2. Mating: predicted chance per co-housed compatible pair vs whether it had kittens that night
    bins, litters = {}, []
    for r in clean:
        bred = {}
        for b in r["births"]:
            bred.setdefault(frozenset((b["sire"], b["dam"])), []).append(b["kitten"])
        for p in r["pairs"]:
            if not p["compatible"]:
                continue
            kittens = bred.get(frozenset((p["a"], p["b"])), [])
            e = bins.setdefault(_bin(p["chance_both_agree"]), {"pairs": 0, "bred": 0, "predicted_sum": 0.0})
            e["pairs"] += 1
            e["bred"] += bool(kittens)
            e["predicted_sum"] += p["chance_both_agree"]
            if kittens:
                litters.append((len(kittens), p["expected_kittens_per_mating"]))
    mating_result = [{"predicted_range": k, "pair_nights": e["pairs"], "had_kittens": e["bred"],
                      "observed_rate": round(e["bred"] / e["pairs"], 3),
                      "mean_predicted": round(e["predicted_sum"] / e["pairs"], 3)}
                     for k, e in sorted(bins.items())]
    unmatched = sum(1 for r in clean for b in r["births"]
                    if not any(frozenset((p["a"], p["b"])) == frozenset((b["sire"], b["dam"])) for p in r["pairs"]))

    return {
        "nights_logged": len(nights), "nights_usable": len(clean), "births_logged": len(births),
        "stimulation_check": stim_result,
        "mating_check": mating_result,
        "litters": {"count": len(litters),
                    "mean_kittens": round(sum(n for n, _ in litters) / len(litters), 2) if litters else None,
                    "mean_expected": round(sum(e for _, e in litters) / len(litters), 2) if litters else None},
        "births_from_pairs_not_housed_together": unmatched,
        "note": "mating_check compares a per-attempt chance with per-night outcomes; how cats pick partners "
                "isn't decoded, so expect observed rates below the prediction when rooms hold several cats",
    }
