Settle - Seat Collision for SMP Cloth  1.1.2 (beta)
====================================================

What it does
- SMP cloth (skirts, robes, capes) collides with the seat of the chair, bench or stool an actor sits on,
  instead of passing through it. Player and NPCs.
- Main version: while seated, the lower cloth of short skirts and robes is tied to the thighs (and shins),
  so it lies on the lap; it is locked once settled to stop jitter. Long skirts keep normal physics + seat collision.
- Riding: skirts no longer flatten into a disc on horseback. The outfit's invisible ground plane is switched off
  while riding (both versions).
  Main version: short skirts and robes are also tied to the thighs while riding.
- Tails (equippable SMP tails) are never tied to the legs.
- Only real seats get the hug: work stations (forge, smelter, workbench, ...) never do, and if an actor's thighs point
  down once settled (guard posts, leaning, kneeling, mining) the hug is removed again (fSeatedMaxThighDrop).

Requirements
- Skyrim SE/AE with SKSE and Address Library for SKSE Plugins. Tested on 1.6.1170.
- Faster HDT-SMP 4.1.1 (by DaymareOn and contributors).
  Settle ships a modified build of it (SKSE\Plugins\hdtsmp64.dll) in AVX, AVX2 and AVX-512 versions;
  the installer asks which one: pick the same variant you chose when installing FSMP. Settle must overwrite FSMP's DLL:
  in MO2 put Settle BELOW Faster HDT-SMP in the left pane; in Vortex make Settle load after it.
  FSMP's other files (configs) are still used.
  The matching hdtsmp64.pdb (debug symbols) is included: mods that read FSMP's internals, such as Banner Physics,
  use it to recognise Settle's build. Keep it next to the DLL.

Settings
- SKSE\Plugins\Settle.ini, re-read every time an actor sits (no restart needed). bEnabled = 0 turns Settle off.
- Per-outfit overrides: SKSE\Plugins\Settle\HugOverrides\*.ini (see _Examples.ini).
  bIgnore = 1 in an item's section makes Settle leave that item alone.
- Hand-tuned seated physics for specific outfits: SKSE\Plugins\Settle\SeatSwap\ (see _ReadMe.txt).
- Log: Documents\My Games\Skyrim Special Edition\SKSE\Settle.log

Known limits
- Only real seats with collision: no effect on invisible sit markers (rocks, ledges, ground), tables or walls.
- Long and slit skirts get seat collision only (no hug); on horseback they hang freely.
- AVX-512 only runs on CPUs that support it; if unsure, choose AVX2 (or AVX on older CPUs).

Changelog
- 1.1.2: the leg hug no longer applies to standing actors using furniture (guard posts, work stations, mining,
  leaning, kneeling), which could stretch their armor or clothes.
- 1.1.1: tails are no longer tied to the legs; bIgnore = 1 in HugOverrides to leave an item alone (paths are
  case-insensitive); hdtsmp64.pdb included for Banner Physics.
- 1.1.0: riding support (ground plane off while riding, riding hug in Main; optional experimental horse collider).
- 1.0.3: skirts no longer pushed up over chair armrests (fSeatAbove).
- 1.0.2: AVX2 and AVX-512 builds.
- 1.0.1: actors already seated when a save loads are handled.

Uninstall
- Remove Settle; FSMP's own hdtsmp64.dll takes over again. Nothing is saved in your save game.

License and source
- hdtsmp64.dll is a modified version of Faster HDT-SMP, licensed GPL-3.0 with the FSMP exceptions
  (Licenses\GPL-3.0.txt, Licenses\FSMP-EXCEPTIONS.txt). Settle.dll is licensed GPL-3.0-or-later (Licenses\GPL-3.0.txt).
- Source code:
    Settle.dll:   https://github.com/Flob26/Settle
    hdtsmp64.dll: https://github.com/Flob26/Settle-hdtSMP64 (branch flobrim-world-collision)

Credits
- DaymareOn and all Faster HDT-SMP / HDT-SMP contributors.
- CommonLibSSE-NG (alandtse and contributors).
