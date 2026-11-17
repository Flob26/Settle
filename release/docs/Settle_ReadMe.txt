Settle - Seat Collision for SMP Cloth  1.0.0 (beta)
====================================================

What it does
- SMP cloth (skirts, robes, capes) collides with the seat of the chair, bench or stool an actor sits on,
  instead of passing through it. Player and NPCs.
- Main version: while seated, the lower cloth of short skirts and robes is tied to the thighs (and shins),
  so it lies on the lap; it is locked once settled to stop jitter. Long skirts keep normal physics + seat collision.

Requirements
- Skyrim SE/AE with SKSE and Address Library for SKSE Plugins. Tested on 1.6.1170.
- Faster HDT-SMP 4.1.1 (by DaymareOn and contributors).
  Settle ships a modified build of it (SKSE\Plugins\hdtsmp64.dll, AVX). Settle must overwrite FSMP's DLL:
  in MO2 put Settle BELOW Faster HDT-SMP in the left pane; in Vortex make Settle load after it.
  FSMP's other files (configs) are still used.

Settings
- SKSE\Plugins\Settle.ini, re-read every time an actor sits (no restart needed). bEnabled = 0 turns Settle off.
- Per-outfit overrides: SKSE\Plugins\Settle\HugOverrides\*.ini (see _Examples.ini).
- Hand-tuned seated physics for specific outfits: SKSE\Plugins\Settle\SeatSwap\ (see _ReadMe.txt).
- Log: Documents\My Games\Skyrim Special Edition\SKSE\Settle.log

Known limits
- Only real seats with collision: no effect on invisible sit markers (rocks, ledges, ground), tables or walls.
- Long and slit skirts get seat collision only (no hug).
- AVX build only (AVX2/AVX512 builds may come later).

Uninstall
- Remove Settle; FSMP's own hdtsmp64.dll takes over again. Nothing is saved in your save game.

License and source
- hdtsmp64.dll is a modified version of Faster HDT-SMP, licensed GPL-3.0 with the FSMP exceptions
  (Licenses\GPL-3.0.txt, Licenses\FSMP-EXCEPTIONS.txt). Settle.dll is licensed GPL-3.0.
- Source code: <GitHub links to be added>

Credits
- DaymareOn and all Faster HDT-SMP / HDT-SMP contributors.
- CommonLibSSE-NG (alandtse and contributors).
