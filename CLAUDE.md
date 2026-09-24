# Mewgenics Cat Bridge

Live-memory mod (C++ DLL injected into Mewgenics) + Python MCP server that
exposes cats, pedigree, rooms and breeding/adventure advice to Claude.

**Read [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) before changing anything**:
architecture, pipe protocol, every reverse-engineered layout and how it was
verified, what is only a hypothesis, known limitations.

## Dev loop (Windows, game running with a save loaded, in the house)

```
powershell -ExecutionPolicy Bypass -File mod\build.ps1 [-Console]   # -> bin\cat_bridge.dll
python tools\cat_bridge_dev.py eject     # before rebuilding (no game restart needed)
python tools\cat_bridge_dev.py inject
python tools\cat_bridge_dev.py send LIST_CATS
```

Test MCP tools by importing `mcp_server/server.py` and calling the functions,
or through `server.mcp.call_tool(...)`. The game folder is found through
Steam (`game_data.find_game_dir`); `MEWGENICS_DIR` overrides.

## Rules

- Game memory is the source of truth. The save file
  (`%APPDATA%\Glaiel Games\Mewgenics\<id>\saves\*.sav`, SQLite) is only for
  validating hypotheses -- read a copy, never the original, never write it.
- A new layout/mechanic isn't "known" until checked against an independent
  source (in-game UI via the player, the .gon data, the save). Record the
  finding and how it was verified in docs/DEVELOPMENT.md. Label unverified
  things as hypotheses in code, tool docs and answers.
- Ask the player before any live write (`SET_*` commands / `set_*` tools)
  and suggest saving first; report the previous value so it can be undone.
- Reverse-engineering scratch scripts go in the session scratchpad, not the repo.
- MCP tool docstrings are what Claude sees at runtime: keep them accurate
  (FastMCP captures them at registration -- don't patch `__doc__` later).

## Releasing

Bump `MOD_VERSION` in `mod/cat_bridge/amoeboid.hpp`, run `mod\build.ps1`, test
the DLL in-game, `python tools\package_release.py` (-> `dist\*.zip`), commit,
push, then `gh release create vX.Y.Z dist\mewgenics-cat-bridge-vX.Y.Z.zip`.
After a game update, first run `python mod\cat_bridge\misc\find_rvas.py` and
update `EXE_SHA256`/`EXE_VERSION` (see docs/DEVELOPMENT.md).
