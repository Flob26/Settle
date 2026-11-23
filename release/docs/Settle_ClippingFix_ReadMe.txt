Settle + Clipping Fix  1.2.0 (optional add-on)
==============================================

One hdtsmp64.dll that contains both:
- Settle's modified Faster HDT-SMP (seat collision interface, seated hug, riding: ground plane off), and
- FSMP Clothing Clipping Fix (FSMP Discontinuity Guard) by Zorkanne33 / YoucefNabil:
  cloth is carried through teleports, reset after sudden pose changes, and held at rest briefly
  after an outfit is equipped, so legs don't end up poking through skirts after loading screens.

Install
- Install the main Settle file first (and Faster HDT-SMP 4.1.1).
- Install this add-on and let it overwrite Settle's hdtsmp64.dll (MO2: place it BELOW Settle in the left pane).
- Do NOT also install FSMP Clothing Clipping Fix itself: this DLL already includes it.
- Uninstall: remove this add-on; plain Settle takes over again.

Known behavior
- When an actor sits down, the skirt can dip below the seat for about a second before it settles on top.
  (The Clipping Fix holds freshly rebuilt physics at rest briefly; Settle rebuilds the outfit's physics on sit.)
- No AVX, AVX, AVX2 and AVX-512 builds: the installer asks which one; pick the same variant you chose for FSMP.
- Clipping Fix settings (guardDiscontinuities, teleportSpeedLimit, ...) can be added to FSMP's configs.json
  under "smp"; the defaults need no changes. See the Clipping Fix page for details.

License and source
- GPL-3.0 with the FSMP exceptions (Licenses\).
- Source: https://github.com/Flob26/Settle-hdtSMP64 (branch settle-guard)
- Clipping Fix source: https://github.com/YoucefNabil/FSMP-Discontinuity-Guard

Credits
- Zorkanne33 / YoucefNabil for FSMP Clothing Clipping Fix (Discontinuity Guard).
- DaymareOn and all Faster HDT-SMP contributors.
