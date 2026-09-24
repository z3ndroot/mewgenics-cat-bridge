# Breeding mechanics research notes

Context: while scoping this project we found a second, unrelated Mewgenics
MCP effort already on GitHub: **jph6366/mewgenics-mcp**
(https://github.com/jph6366/mewgenics-mcp). It takes a completely different
approach from this project -- it parses **save files** (offline analysis)
rather than injecting into the live process, and focuses on breeding/combat
*analytics* rather than live read/write access. No overlap in approach, but
its `IMPLEMENTATION_GUIDE.md` contains a breeding-mechanics model that's
worth mining, because `cat_bridge` gives us something that project doesn't
have: live ground truth to check the model against.

**Important caveat:** the formulas below are that repo author's own
reconstruction/guess at Mewgenics' mechanics (explicitly commented
"simplified" in their source), not confirmed official game formulas or
values pulled from this project's own reverse engineering. Treat as a
hypothesis to validate, not a known-good spec.

## The model (as found in jph6366/mewgenics-mcp)

**Stat inheritance** depends on the house's Stimulation stat. For each of
the 7 stats (str/dex/con/int/spd/cha/lck) independently:
- Stimulation >= 196: kitten always inherits the higher parent value
- Stimulation >= 95: 70% chance of the higher value, 30% the lower
- Stimulation >= 32: 55% chance of the higher value, 45% the lower
- Below 32: uniform random pick between the two parent values

**Inbreeding coefficient:**
```
F_child = 0.5 * (F_parent1 + F_parent2) + kinship
```
where `kinship` is 0 for unrelated parents, 0.5 for parent-child, etc.

**Mutation inheritance** (per body part -- body/head/tail/leg/arm/eye/
eyebrow/ear/mouth/fur):
- 20% chance: mutation is regenerated at random regardless of parents
- Otherwise: if both parents have a mutation on that part, pick one of the
  two at random; if only one parent does, it's favored to pass on; if
  neither does, no mutation

**Disorder/birth-defect inheritance:**
- 15% independent chance to inherit a disorder from the mother
- 15% independent chance to inherit a disorder from the father
- If fewer than 2 disorders were inherited that way, roll for an
  additional birth defect with chance `0.02 + 0.4 * max(F - 0.2, 0)`
  (F = the child's inbreeding coefficient above)

## Why this is checkable with cat_bridge specifically

`CatData` (see `mod/cat_bridge/types/glaiel_cat.hpp`) already has a `coi`
field (`double coi;`) right next to `fertility`/`aggression`/etc. -- almost
certainly "coefficient of inbreeding", i.e. the same quantity `F` above.
That's a strong signal the model's shape (at least for inbreeding) is
roughly on the right track, and that the real value is directly readable
once `cat_bridge` is working.

Concrete validation idea for once the MVP round-trips (list_cats/get_cat
working against a real running game):
1. Pick two cats, record their `stats` (heritable), `coi`, and any visible
   mutations via `get_cat`.
2. Breed them in-game.
3. Read the resulting kitten's `coi`, stats, and mutations the same way.
4. Compare against what the formulas above would have predicted, across
   enough repeats to see the *distribution* (especially for the
   probabilistic stat-inheritance rule) -- a few dozen breedings if that's
   practical, not just one.
5. If it matches: good, treat the model as validated and it's safe to build
   a `predict_breeding_outcome`-style MCP tool on top of it. If it doesn't:
   the real house Stimulation value and its thresholds, and the exact
   inbreeding formula, are things worth re-deriving from observed data
   instead -- which is a much stronger position to be in than either
   existing project, since this one can actually generate labeled
   before/after data automatically.

This isn't a required part of the MVP (get basic list/read/write working
first, per docs/DEVELOPMENT.md) -- it's a good phase-2 feature once the pipe/MCP
round-trip is solid.
