# Fuse Architecture Map

## Purpose
This map links major Fuse subsystems to their primary source files and supporting documentation so contributors can quickly locate implementation and reference material.

## Core System
- **Purpose**
  - Central Fuse runtime, API surface, and top-level system integration.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/Fuse.cpp`
  - `soh/soh/Enhancements/Fuse/Fuse.h`
  - `soh/soh/Enhancements/FuseSystem.cpp`
- **Related docs**
  - [`FuseArchitectureOverview.md`](./FuseArchitectureOverview.md)
  - [`FuseCoreSystem.md`](./FuseCoreSystem.md)
- **Notes**
  - Primary entry point for core Fuse behavior and shared runtime operations.

## State and Persistence
- **Purpose**
  - Save/runtime Fuse slot state and persistence helpers.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/FuseState.cpp`
  - `soh/soh/Enhancements/Fuse/FuseState.h`
- **Related docs**
  - [`FuseState.md`](./FuseState.md)
- **Notes**
  - Owns persistent/runtime Fuse slot and inventory state boundaries.

## Materials and Modifiers
- **Purpose**
  - Material definitions and modifier lookup/application support.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/FuseMaterials.cpp`
  - `soh/soh/Enhancements/Fuse/FuseMaterials.h`
  - `soh/soh/Enhancements/Fuse/FuseModifiers.cpp`
  - `soh/soh/Enhancements/Fuse/FuseModifiers.h`
- **Related docs**
  - [`FuseMaterials.md`](./FuseMaterials.md)
  - [`FuseModifiers.md`](./FuseModifiers.md)
- **Notes**
  - Defines what can be fused and which gameplay modifiers each material exposes.

## Gameplay Hooks / Runtime Integrations
- **Purpose**
  - Weapon/projectile/object hook points that connect engine events to Fuse behavior.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.cpp`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Boomerang.h`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.cpp`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Objects.h`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.cpp`
  - `soh/soh/Enhancements/Fuse/Hooks/FuseHooks_Ranged.h`
- **Related docs**
  - [`FuseHooks_Boomerang.md`](./FuseHooks_Boomerang.md)
  - [`FuseHooks_Objects.md`](./FuseHooks_Objects.md)
  - [`FuseHooks_Ranged.md`](./FuseHooks_Ranged.md)
- **Notes**
  - Includes boomerang, object-interaction, and ranged projectile Fuse paths.

## Menus and UI
- **Purpose**
  - Fuse UI windows/menus and pause-flow bridge code.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/RangedFuseMenu.cpp`
  - `soh/soh/Enhancements/Fuse/RangedFuseMenu.h`
  - `soh/soh/Enhancements/Fuse/UI/FuseMenuWindow.cpp`
  - `soh/soh/Enhancements/Fuse/UI/FuseMenuWindow.h`
  - `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`
  - `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.h`
- **Related docs**
  - [`FusePauseMenu.md`](./FusePauseMenu.md)
  - [`FuseRangedMenu.md`](./FuseRangedMenu.md)
- **Notes**
  - Pause bridge files handle integration of Fuse UI into pause/Kaleido flow.

## Visuals / Presentation
- **Purpose**
  - Visual attachment/draw handling for Fuse presentation.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/Visuals/FuseVisual.cpp`
  - `soh/soh/Enhancements/Fuse/Visuals/FuseVisual.h`
- **Related docs**
  - [`FuseVisuals.md`](./FuseVisuals.md)
- **Notes**
  - Visual-only layer for rendering Fuse-related attachments/effects.

## C/C++ Bridge / Cross-Boundary Integration
- **Purpose**
  - C ABI bridge for Fuse functionality used across C/C++ boundaries.
- **Primary files**
  - `soh/soh/Enhancements/Fuse/FuseCBridge.h`
- **Related docs**
  - [`FuseCBridge.md`](./FuseCBridge.md)
- **Notes**
  - Exposes C-compatible declarations while Fuse internals remain in C++.

## Special Actor / Overlay Components
- **Purpose**
  - Fuse-specific actor/overlay component for beam behavior.
- **Primary files**
  - `soh/src/overlays/actors/ovl_En_Fuse_Beam/z_en_fuse_beam.c`
  - `soh/src/overlays/actors/ovl_En_Fuse_Beam/z_en_fuse_beam.h`
- **Related docs**
  - [`FuseArchitectureOverview.md`](./FuseArchitectureOverview.md)
- **Notes**
  - Contains a Fuse-specific overlay actor touchpoint (`En_Fuse_Beam`).

## Regression / Validation
- **Purpose**
  - Checklist-based validation guidance after Fuse changes.
- **Primary files**
  - _N/A (documentation-guided validation)_
- **Related docs**
  - [`RegressionChecklist.md`](./RegressionChecklist.md)
- **Notes**
  - Use as a quick post-change verification matrix by subsystem.
