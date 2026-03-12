# Fuse File Index

Fuse documentation in this repo is now aligned to the Shipwright-root layout.
All paths below are **repo-relative** (no shadow-layout prefixes).

## Core runtime / integration
- `soh/soh/Enhancements/FuseSystem.cpp` — top-level Fuse initialization and registration entrypoint.
- `soh/soh/Enhancements/Fuse/Fuse.cpp`
- `soh/soh/Enhancements/Fuse/Fuse.h`
- `soh/soh/Enhancements/Fuse/FuseState.cpp`
- `soh/soh/Enhancements/Fuse/FuseState.h`
- `soh/soh/Enhancements/Fuse/FuseCBridge.h`

## Materials, modifiers, and rules
- `soh/soh/Enhancements/Fuse/FuseMaterials.cpp`
- `soh/soh/Enhancements/Fuse/FuseMaterials.h`
- `soh/soh/Enhancements/Fuse/FuseModifiers.cpp`
- `soh/soh/Enhancements/Fuse/FuseModifiers.h`
- `soh/soh/Enhancements/Fuse/ShieldBashRules.cpp`
- `soh/soh/Enhancements/Fuse/ShieldBashRules.h`

## Hooks
- `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.cpp`
- `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h`
- `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.cpp`
- `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.h`
- `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.cpp`
- `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.h`

## UI
- `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`
- `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.h`
- `soh/soh/Enhancements/Fuse/UI/FuseMenuWindow.cpp`
- `soh/soh/Enhancements/Fuse/UI/FuseMenuWindow.h`
- `soh/soh/Enhancements/Fuse/RangedFuseMenu.cpp`
- `soh/soh/Enhancements/Fuse/RangedFuseMenu.h`

## Visuals
- `soh/soh/Enhancements/Fuse/Visuals/FuseVisual.cpp`
- `soh/soh/Enhancements/Fuse/Visuals/FuseVisual.h`

## Actor / overlay touchpoint
- `soh/src/overlays/actors/ovl_En_Fuse_Beam/z_en_fuse_beam.c`
- `soh/src/overlays/actors/ovl_En_Fuse_Beam/z_en_fuse_beam.h`

## Adjacent runtime files commonly involved during Fuse work
- `soh/src/overlays/actors/ovl_En_Arrow/z_en_arrow.c`
- `soh/src/overlays/actors/ovl_Arms_Hook/z_arms_hook.c`
- `soh/src/overlays/actors/ovl_player_actor/z_player.c`
- `soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c`
