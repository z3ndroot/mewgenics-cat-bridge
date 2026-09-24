# Mewgenics Cat Bridge

**Let Claude see and manage your cats in a running game of [Mewgenics](https://store.steampowered.com/app/686060/Mewgenics/).**

A small mod DLL reads the game's memory live and a Python [MCP](https://modelcontextprotocol.io) server turns it into tools an AI assistant can use: your cats' stats, abilities, mutations and family trees, the rooms of your house, and advice on breeding, strays and adventures. It can also edit cats (stats, passives, body parts).

[Русская версия](README.ru.md)

> Unofficial fan project. Not affiliated with or endorsed by Edmund McMillen, Tyler Glaiel or the publishers of Mewgenics.

## What you can ask

- *"Which two cats should I breed for strength and constitution, without inbreeding?"*
- *"Plan three generations of breeding towards all 7s."*
- *"Is today's stray worth adopting?"*
- *"Which cats aren't needed for breeding? Build me an adventure party and pick their collars."*
- *"How should I set up the Floor1_Small room for breeding?"*
- *"Show me Sosakus's family tree. Is Dushenka inbred?"*
- *"Give Bukhoslav the Toad Style passive."* / *"Remove Pox from Grimurir."*

## Features

| Area | Tools | What they do |
|---|---|---|
| Cats | `list_cats`, `get_cat` | Stats exactly as shown in-game, split into base (heritable) and bonus parts (class, items, passives, mutations, injuries); abilities and passives with in-game names (English and Russian); mutations; room; the daily stray |
| Family | `get_family` | Parents, ancestors, siblings, children and inbreeding coefficient for every cat the save has ever had |
| Breeding | `evaluate_pair`, `suggest_breeding_pairs`, `plan_breeding` | Kinship / kitten COI, sex and orientation compatibility, which stats, mutations and abilities a kitten can inherit and how likely, multi-generation plans |
| Birth log | `birth_log_report` | Records every in-game night in the background (who lived where, room stats, predicted mating chances, kittens born) and checks the breeding hypotheses against real births |
| Strays & roles | `evaluate_strays`, `breeding_roles` | Whether to adopt the stray waiting outside; which cats the bloodline needs and which are free to risk |
| Adventures | `suggest_adventure_team` | A party of cats not needed for breeding, with a class (collar) for each |
| House | `get_rooms`, `suggest_room_setup` | Room stats as shown in-game (Comfort incl. crowding, Stimulation, Evolution, Health, Appeal), furniture per room, what to move where, what to look for in shops |
| Editing | `set_cat_stat`, `set_cat_passive`, `find_passives`, `set_cat_body_part`, `add_cat_mutation`, `find_mutations`, `set_cat_hp` | Change base stats, passives / disorders and body parts; search mutations by effect or stat and give one to a cat; every edit returns the old value so it can be undone |

## Requirements

- Windows, Mewgenics from Steam — **game version 1.1.21239** (see [Game updates](#game-updates))
- [Python 3.10+](https://www.python.org/downloads/windows/), **64-bit**, with "Add python.exe to PATH" ticked
- An MCP client: [Claude Desktop](https://claude.ai/download) or [Claude Code](https://docs.claude.com/en/docs/claude-code)

## Installation

1. Download `mewgenics-cat-bridge-vX.Y.Z.zip` from [Releases](https://github.com/z3ndroot/mewgenics-cat-bridge/releases/latest) and unzip it anywhere, e.g. `C:\mewgenics-cat-bridge`.
2. Install the Python packages — open a terminal in that folder and run:
   ```
   pip install -r requirements.txt
   ```
3. Connect it to Claude.

   **Claude Desktop** — Settings → Developer → Edit Config, and add (use your own path, with double backslashes):
   ```json
   {
     "mcpServers": {
       "mewgenics": {
         "command": "python",
         "args": ["C:\\mewgenics-cat-bridge\\mcp_server\\server.py"]
       }
     }
   }
   ```
   Restart Claude Desktop.

   **Claude Code**:
   ```
   claude mcp add mewgenics -- python C:\mewgenics-cat-bridge\mcp_server\server.py
   ```

## Every play session

1. Start Mewgenics and load your save (be in the house).
2. Double-click **`inject.bat`**. It should say `injected ... into pid ...`.
3. Talk to Claude.

While Claude Desktop is open, the server also keeps a **birth log** in the background (`%LOCALAPPDATA%\mewgenics-cat-bridgeirth_log.jsonl`); ask Claude for `birth_log_report` after some in-game nights. To log without Claude Desktop, run `python toolsirth_logger.py`; set `MEWGENICS_BIRTH_LOG=off` to disable it.

The mod stays loaded until you close the game; run `inject.bat` again after each game start. `eject.bat` unloads it without closing the game.

The game folder is found automatically through Steam. If yours isn't found, set the environment variable `MEWGENICS_DIR` to the folder that contains `Mewgenics.exe`.

## Safety

- **Reading never changes anything.** Edits (`set_*` tools) change the running game's memory, and the game saves them like any other change. Claude should ask before editing — and you can always ask it to undo, since every edit reports the previous value.
- **Save before experimenting.** The game also keeps its own backups in `%APPDATA%\Glaiel Games\Mewgenics\<id>\saves\backups`.
- The mod checks the game's exact version (SHA-256 of `Mewgenics.exe`) and stays **inactive** on any other version instead of guessing.

## Game updates

This release is built for Mewgenics **1.1.21239**. After a game update the mod stays inactive (tools answer "timed out" / "could not connect") until a new release adds the new version. If you build from source you can usually update it yourself in a minute — see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#after-a-game-update).

## Troubleshooting

| Message | Fix |
|---|---|
| `could not connect to \\.\pipe\cat_bridge` | The mod isn't loaded: start the game, load the save, run `inject.bat` |
| `Mewgenics.exe is not running` | Start the game before `inject.bat` |
| `needs 64-bit Python` | Install the 64-bit Python build |
| `timed out waiting for game thread` | The game is on a loading screen or frozen in the background; switch back to it. Also the symptom of an unsupported game version |
| `game data unavailable` / stats without class bonuses | `resources.gpak` wasn't found — set `MEWGENICS_DIR` |
| Only some cats listed | Be in the house; other screens aren't supported yet |

## How it works

```
Mewgenics.exe ── cat_bridge.dll (injected) ── named pipe ── mcp_server (Python) ── Claude
                 reads/writes game memory                   + game data from resources.gpak
```

The DLL hooks the game's per-frame update and answers one command per frame on the game thread, so it never touches memory while the game is changing it. Everything it knows was reverse-engineered and checked against the game's UI, its data files and its save format; [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) lists what is verified and what is still a hypothesis. In short:

- **Verified in-game:** displayed stat formula, heritable (base) stats, body-part mutations and how they stack, room Comfort with crowding penalty, pedigree and inbreeding coefficient (matches the game on every cat), who lives in the house vs. the stray, stat / passive / body-part editing.
- **Measured on a real save:** ability inheritance (≈32% a parent's innate ability, ≈2% a learned one, ≈66% random).
- **Read from the game's code:** the cat info labels (libido, aggression, inbreeding, orientation; checked in-game) and the mating formula (libido, orientation, partner's charisma, room Comfort, fertility) — the birth log is there to check it on real nights.
- **Hypothesis:** how Stimulation changes the odds of inheriting the better stat (thresholds 32 / 95 / 196 from [another project](https://github.com/jph6366/mewgenics-mcp)) — the tools say so when they use it.

## Building from source

Needs Visual Studio 2022+ (or Build Tools) with "Desktop development with C++".

```
git clone https://github.com/z3ndroot/mewgenics-cat-bridge
cd mewgenics-cat-bridge
pip install -r requirements.txt
powershell -ExecutionPolicy Bypass -File mod\build.ps1      # -> bin\cat_bridge.dll
```

Add `-Console` to get a log window next to the game. See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the architecture, the pipe protocol and how to reverse-engineer new structures.

## Credits

- The mod is built on [mewgenics_randomize_item_picks](https://github.com/p0lymeric/mewgenics_randomize_item_picks) by **polymeric** (MIT) — the base DLL framework, signature scanning and the reconstructed game structures.
- Libraries: Detours, LibTomCrypt, Mewjector, SDL headers, pefile — see [mod/ATTRIBUTION.md](mod/ATTRIBUTION.md).
- Breeding-model hypotheses: [jph6366/mewgenics-mcp](https://github.com/jph6366/mewgenics-mcp) and the Steam community guide by RedettFZ.

## License

[MIT](LICENSE). Parts of `mod/` are © polymeric under MIT; vendored libraries keep their own licenses.
