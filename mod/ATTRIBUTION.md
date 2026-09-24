## Attribution

This project (cat_bridge) is built on top of the "amoeboid" base DLL
framework and reconstructed Mewgenics type definitions (types/glaiel_*.hpp,
utilities/*) from:

* mewgenics_randomize_item_picks
  * https://github.com/p0lymeric/mewgenics_randomize_item_picks
  * Copyright (c) polymeric. MIT License.
  * `amoeboid.cpp`, `utilities/*`, `types/*`, and `misc/find_rvas.py` here
    are adapted/copied from that project with minimal changes. See its
    LICENSE.md (copied alongside this file) for the full text.

It additionally gratefully uses the following libraries, vendored by the
above project and reused here unchanged.

### C/C++

* Detours
  * Used for function hooking and DLL injection.
  * `Copyright (c) Microsoft Corporation.`
  * MIT License
  * https://github.com/microsoft/Detours/blob/main/LICENSE
* LibTomCrypt
  * Used to hash Mewgenics.exe to detect version mismatches.
  * `LibTomCrypt, modular cryptographic library -- Tom St Denis`
  * Unlicense
  * https://github.com/libtom/libtomcrypt/blob/develop/LICENSE
* Mewjector
  * Used for coordinated function hooking and logging.
  * `Copyright (c) 2026 Mewjector Contributors`
  * MIT License
  * https://github.com/githubuser508/mewjector/blob/main/LICENSE
* SDL3
  * Used to interface with Mewgenics' game engine.
  * `Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>`
  * zlib License
  * https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt
* STL
  * Referenced to write `types/msvc.hpp`.
  * `Copyright (c) Microsoft Corporation.`
  * Apache License v2.0 with LLVM Exception
  * https://github.com/microsoft/STL/blob/main/LICENSE.txt

### Python

* pefile
  * Used by `find_rvas.py` to parse Mewgenics.exe's PE structures for signature scanning.
  * `Copyright (c) 2004-2024 Ero Carrera`
  * MIT License
  * https://github.com/erocarrera/pefile/blob/master/LICENSE
