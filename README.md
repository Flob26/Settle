# Settle - Seat Collision for SMP Cloth

SKSE plugin for Skyrim SE/AE: SMP cloth (skirts, robes, capes) collides with the seat an actor sits on, and
short skirts lie on the legs while seated. Player and NPCs.

Requires the Settle build of Faster HDT-SMP: https://github.com/Flob26/Settle-hdtSMP64
(branch `flobrim-world-collision`), which adds the world-collision interface in `include/WorldCollisionAPI.h`.

## Build
Windows, Visual Studio 2022/2026 with the v143 toolset, CMake, vcpkg (`VCPKG_ROOT`). Run `build.bat`
(clones CommonLibSSE-NG into `extern/`, builds Release, copies `Settle.dll` into the dev MO2 mod).

## Layout
- `src/main.cpp` - the plugin
- `data/` - files shipped in `Data/` (Settle.ini, HugOverrides example, SeatSwap readme)
- `release/` - FOMOD installer XML and the user readme

## License
GPL-3.0-or-later (see `LICENSE`).
