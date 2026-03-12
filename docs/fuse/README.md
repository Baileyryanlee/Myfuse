# Fuse System Documentation

## Overview
The Fuse system in the Myfuse mod adds gameplay support for attaching materials to weapons/items and applying fuse-driven behavior through material properties, modifiers, and related integration logic.

This page is a developer-focused entry point to the Fuse documentation set in `docs/fuse/`.

## Documentation Map

### Architecture
- [FuseArchitectureOverview.md](./FuseArchitectureOverview.md) - High-level architecture and subsystem boundaries.
- [FuseCoreSystem.md](./FuseCoreSystem.md) - Core runtime flow and Fuse behavior handling.
- [FuseState.md](./FuseState.md) - Runtime and persistent state tracking for fused items.
- [FuseCBridge.md](./FuseCBridge.md) - C/C++ bridge layer used by Fuse systems.

### Materials and Modifiers
- [FuseMaterials.md](./FuseMaterials.md) - Material definitions, metadata, and behavior-facing properties.
- [FuseModifiers.md](./FuseModifiers.md) - Modifier model, rules, and gameplay effects.

### Gameplay Integration
- [FuseHooks_Boomerang.md](./FuseHooks_Boomerang.md) - Boomerang-specific Fuse hooks.
- [FuseHooks_Objects.md](./FuseHooks_Objects.md) - Object interaction and actor-side hook behavior.
- [FuseHooks_Ranged.md](./FuseHooks_Ranged.md) - Projectile and ranged-item integration.

### UI Systems
- [FusePauseMenu.md](./FusePauseMenu.md) - Pause-menu Fuse interaction flow.
- [FuseRangedMenu.md](./FuseRangedMenu.md) - Ranged Fuse selection and related UI behavior.

### Visual Systems
- [FuseVisuals.md](./FuseVisuals.md) - Visual presentation and effects for fused items.

### Testing / Maintenance
- [RegressionChecklist.md](./RegressionChecklist.md) - Regression checklist for validating Fuse functionality.

## Development Notes
Fuse implementation code primarily lives under:
- `soh/soh/Enhancements/Fuse/`
- `soh/soh/Enhancements/Fuse/Hooks/`
- `soh/soh/Enhancements/Fuse/UI/`
- `soh/soh/Enhancements/Fuse/Visuals/`

Fuse actor overlay:
- `soh/src/overlays/actors/ovl_En_Fuse_Beam/`

## File Index Reference
For a broader cross-repository Fuse file navigation index, see:
- `docs/FUSE_FILE_URL_INDEX.md`
