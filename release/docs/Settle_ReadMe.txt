Settle - Seat Collision for SMP Cloth  1.1.0 (beta)
====================================================

What it does
- SMP cloth (skirts, robes, capes) collides with the seat of the chair, bench or stool an actor sits on,
  instead of passing through it. Player and NPCs.
- Main version: while seated, the lower cloth of short skirts and robes is tied to the thighs (and shins),
  so it lies on the lap; it is locked once settled to stop jitter. Long skirts keep normal physics + seat collision.
- Riding: skirts no longer flatten into a disc on horseback. The outfit's invisible ground plane is switched off
  while riding and a collider over the horse's back keeps cloth out of the horse (both versions).
  Main version: short skirts and robes are also tied to the thighs while riding.

Requirements
- Skyrim SE/AE with SKSE and Address Library for SKSE Plugins. Tested on 1.6.1170.
- Faster HDT-SMP 4.1.1 (by DaymareOn and contributors).
  Settle ships a modified build of it (SKSE\Plugins\hdtsmp64.dll) in AVX, AVX2 and AVX-512 versions;
  the installer asks which one: pick the same variant you chose when installing FSMP. Settle must overwrite FSMP's DLL:
  in MO2 put Settle BELOW Faster HDT-SMP in the left pane; in Vortex make Settle load after it.
  FSMP's other files (configs) are still used.

Settings
- SKSE\Plugins\Settle.ini, re-read every time an actor sits (no restart needed). bEnabled = 0 turns Settle off.
- Per-outfit overrides: SKSE\Plugins\Settle\HugOverrides\*.ini (see _Examples.ini).
- Hand-tuned seated physics for specific outfits: SKSE\Plugins\Settle\SeatSwap\ (see _ReadMe.txt).
- Log: Documents\My Games\Skyrim Special Edition\SKSE\Settle.log

Known limits
- Only real seats with collision: no effect on invisible sit markers (rocks, ledges, ground), tables or walls.
- Long and slit skirts get seat collision only (no hug); on horseback they hang freely.
- Some long dresses sit slightly raised on benches because of their own collision setup (also without Settle).
- AVX-512 only runs on CPUs that support it; if unsure, choose AVX2 (or AVX on older CPUs).

Changelog
- 1.1.0: riding support (ground plane off while riding, horse collider, riding hug in Main).
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
