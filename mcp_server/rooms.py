"""
Rooms, furniture and what to move where -- on top of the DLL's ROOMS command.

Verified (2026-09-24, v1.1.21239): a room's effect totals are exactly the
sum of its furniture's effects from data/furniture_effects.gon, and the
Comfort shown in-game is that total minus 1 per cat beyond the first four.
Not handled: furniture set bonuses, rare furniture (reportedly 2x effects;
none present when this was verified), furniture in storage, and whether a
piece physically fits in the destination room's grid.

The breeding-specific effects in data/furniture_effects_guide.gon
(InheritStatFavorBest, KittenChanceOf..., IncreaseFertility, ...) are not
used by any furniture in v1.1.21239; in practice rooms act through
Comfort, Stimulation, Evolution, Health and Appeal. GOALS keeps weights for
them anyway in case a game update starts using them.
"""

from game_data import EFFECT_NOTES, ROOM_CAT_COMFORT_FREE, furniture_effects

# How much each effect matters for a room's job. Positive = want more.
GOALS = {
    "breeding": {
        "Stimulation": 3, "Comfort": 2, "InheritStatFavorBest": 6, "InheritPieceFavorMutation": 2,
        "InheritAbilityChance": 2, "InheritSecondAbilityChance": 1, "InheritPassiveChance": 2,
        "IncreaseRoomBreedChance": 3, "IncreasePartnerBreedChance": 2, "IncreaseFertility": 3,
        "Health": 0.5, "DecreaseRoomFightChance": 1,
        "BreedSuppression": -20, "DecreaseRoomBreedChance": -6, "InheritDisorderChance": -4,
        "IncreaseRoomFightChance": -2, "FightRisk": -2,
    },
    "mutation": {
        "Evolution": 4, "Stimulation": 1, "InheritPieceFavorMutation": 3, "Comfort": 0.5,
    },
    "nursery": {  # raising kittens / keeping good cats healthy
        "Comfort": 2, "Health": 3, "IncreaseLifespan": 2, "DecreaseRoomFightChance": 1,
        "IncreaseRoomFightChance": -2, "FightRisk": -2,
    },
    "fight_club": {  # winners of house fights gain +1 stat (community guide)
        "Evolution": 2, "Comfort": -1, "IncreaseRoomFightChance": 2, "FightBonusRewards": 2,
    },
}


def load_rooms(rooms_resp):
    """Rooms from the ROOMS command, with per-piece effects and the values the
    game displays (Comfort after the crowding penalty)."""
    table = furniture_effects()
    rooms = []
    for r in rooms_resp["rooms"]:
        pieces = []
        for f in r["furniture"]:
            info = table.get(f["type"], {})
            pieces.append(dict(f, effects=info.get("effects", {}), set=info.get("set")))
        displayed = dict(r["effects"])
        crowding = max(0, len(r["cats"]) - ROOM_CAT_COMFORT_FREE)
        displayed["Comfort"] = displayed.get("Comfort", 0.0) - crowding
        rooms.append({
            "name": r["name"],
            "cats": r["cats"],
            "furniture_totals": r["effects"],
            "crowding_penalty": crowding,
            "displayed": displayed,
            "furniture": pieces,
        })
    return rooms


def piece_value(effects, weights):
    return sum(weights.get(k, 0) * v for k, v in effects.items())


def suggest_setup(rooms, room_name, goal="breeding", max_moves=8):
    """What to move into / out of a room for a goal, using only furniture
    that's already placed somewhere in the house."""
    weights = GOALS[goal]
    target = next((r for r in rooms if r["name"] == room_name), None)
    if target is None:
        raise KeyError(room_name)
    remove = []
    for p in target["furniture"]:
        v = piece_value(p["effects"], weights)
        if v < 0:
            remove.append({"id": p["id"], "type": p["type"], "effects": p["effects"], "value_for_goal": v})
    move_in = []
    for other in rooms:
        if other is target:
            continue
        for p in other["furniture"]:
            v = piece_value(p["effects"], weights)
            if v > 0:
                move_in.append({"id": p["id"], "type": p["type"], "from_room": other["name"],
                                "effects": p["effects"], "value_for_goal": v})
    move_in.sort(key=lambda m: -m["value_for_goal"])
    remove.sort(key=lambda m: m["value_for_goal"])
    move_in = move_in[:max_moves]

    after = dict(target["furniture_totals"])
    for m in move_in:
        for k, v in m["effects"].items():
            after[k] = after.get(k, 0.0) + v
    for m in remove:
        for k, v in m["effects"].items():
            after[k] = after.get(k, 0.0) - v
    after = {k: v for k, v in after.items() if v}
    after_displayed = dict(after)
    after_displayed["Comfort"] = after.get("Comfort", 0.0) - target["crowding_penalty"]

    # what the donor rooms look like afterwards: stripping Comfort from a
    # crowded room turns it into a fight pit
    donors = {}
    for m in move_in:
        donors.setdefault(m["from_room"], []).append(m)
    donor_effects = []
    for other in rooms:
        if other["name"] not in donors:
            continue
        comfort = other["displayed"].get("Comfort", 0.0) - sum(m["effects"].get("Comfort", 0.0)
                                                              for m in donors[other["name"]])
        entry = {"room": other["name"], "cats": len(other["cats"]), "comfort_after": comfort}
        if comfort <= 0:
            entry["warning"] = "Comfort would drop to <= 0 there: cats will fight. Move cats out or skip Comfort pieces."
        donor_effects.append(entry)

    # furniture worth looking for (shops / runs), best for this goal first
    in_house = {p["type"] for r in rooms for p in r["furniture"]}
    shopping = []
    for ftype, info in furniture_effects().items():
        if info["removed"] or ftype == "poop":
            continue
        v = piece_value(info["effects"], weights)
        if v > 0:
            shopping.append({"type": ftype, "effects": info["effects"], "value_for_goal": v,
                             "already_in_house": ftype in in_house})
    shopping.sort(key=lambda s: -s["value_for_goal"])

    cats_note = None
    if target["crowding_penalty"]:
        cats_note = (f"{len(target['cats'])} cats here: each cat beyond {ROOM_CAT_COMFORT_FREE} costs 1 Comfort "
                     f"(-{target['crowding_penalty']} now). For {goal}, keep only the cats that should be here.")
    # data/furniture_effects_guide.gon lists many breeding effects
    # (InheritStatFavorBest, KittenChanceOf..., IncreaseFertility, ...) that
    # no furniture actually uses in v1.1.21239 -- only mention real ones.
    used = {k for info in furniture_effects().values() for k in info["effects"]}
    return {
        "room": room_name,
        "goal": goal,
        "what_matters": {k: EFFECT_NOTES.get(k, "") for k, w in weights.items() if w > 0 and k in used},
        "now": target["displayed"],
        "move_in": move_in,
        "remove": remove,
        "after_if_all_applied": after_displayed,
        "donor_rooms_after": donor_effects,
        "worth_acquiring": shopping[:10],
        "cats_note": cats_note,
        "caveats": [
            "moving a piece also changes the room it comes from",
            "doesn't check whether a piece fits the destination grid",
            "furniture in storage / shops isn't visible to this tool yet",
            "set bonuses and rare (double-strength) pieces aren't modelled",
        ],
    }
