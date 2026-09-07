# FuseRangedMenu Implementation Reference

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseHooks_Ranged](./FuseHooks_Ranged.md), [FusePauseMenu](./FusePauseMenu.md), [FuseMaterials](./FuseMaterials.md).

## Repository path anchors (Shipwright-root)
- Ranged hold menu: `soh/soh/Enhancements/Fuse/RangedFuseMenu.cpp`, `soh/soh/Enhancements/Fuse/RangedFuseMenu.h`

## Purpose / scope
- `RangedFuseMenu` is the hold-to-open in-game ranged fuse menu (`Update` + `Draw`).
- It is distinct from pause modal flow in `FusePauseMenu` docs.

## Open/close/input flow
- Open conditions (`Update`):
  - `play` exists and `Fuse::IsEnabled()`.
  - player is aiming a supported ranged weapon (from `IsPlayerAimingRangedWeapon`).
  - custom fuse hold input active (`IsFuseMenuHeld` -> `BTN_CUSTOM_FUSE_MENU`).
  - `reopenCooldownTimer <= 0`.
- Close conditions:
  - stop aiming -> immediate `CloseMenu()`.
  - release hold button -> `CommitSelection()` then `CloseMenu()`.
- Input consumption while open:
  - D-pad up/down or stick Y above `kStickThreshold`.
  - `HandleNavigation` clears `BTN_DUP | BTN_DDOWN` from `press` and `cur` to consume nav input.

## Weapon/type branch conditions
- `IsPlayerAimingRangedWeapon` maps `heldItemAction` to slot:
  - Bow actions (`PLAYER_IA_BOW`, fire/ice/light variants, etc.) -> `RangedFuseSlotId::Arrows`.
  - `PLAYER_IA_SLINGSHOT` -> `RangedFuseSlotId::Slingshot`.
  - `PLAYER_IA_HOOKSHOT` / `PLAYER_IA_LONGSHOT` -> `RangedFuseSlotId::Hookshot`.
- Slot helpers branch by slot enum:
  - read current: `GetSlotMaterial`.
  - clear: `ClearSlot` (`Fuse::ClearArrowsFuse`, `Fuse::ClearSlingshotFuse`, `Fuse::ClearHookshotFuse`).
  - fuse attempt: `TryFuseSlot` (`Fuse::TryFuseArrows`, `Fuse::TryFuseSlingshot`, `Fuse::TryFuseHookshot`).

## State model
- `RangedFuseMenuState` fields:
  - `isOpen`
  - `selectedIndex`
  - `scrollOffset`
  - `reopenCooldownTimer`
  - `navRepeatTimer`
  - `weapon`
  - `slot`
  - `entries` (`MaterialEntry` list with id/def/quantity)
- Entry list lifecycle:
  - rebuilt on open and each frame while open (`BuildEntries`).
  - always prepends `MaterialId::None` entry.

## Queue/commit/cancel interactions
- This file directly applies selection to slot state on release (not a deferred queue in this TU):
  - if selected `None`: clear current slot if fused.
  - if selected non-`None` and differs from current: clear current then `TryFuseSlot`.
- TODO (verify): queue/commit interplay with ranged hook/core symbols if behavior changes:
  - `Fuse::TryQueueRangedFuse`
  - `Fuse::CommitQueuedRangedFuse`
  - `Fuse::CancelQueuedRangedFuse_Refund`

## Rendering model
- Overlay panel rendered in `Draw` via:
  - `DrawSolidRectOpa` for background/highlight.
  - `GfxPrint` text list with selected row color.
- Visible row count clamps against both constants and screen height.

## CVars referenced in this file
- None.

## Debug log tags/prefixes in this file
- `[FuseDBG]`:
  - `RangedFuseMenu: Open ...`
  - `RangedFuseMenu: Close`
  - `RangedFuseMenu: Select ...`

## Common failure modes
- Menu never opens:
  - inspect `IsFuseMenuHeld`, `IsPlayerAimingRangedWeapon`, `reopenCooldownTimer` gate.
- Selection appears to revert:
  - inspect `CommitSelection`, per-slot clear/fuse calls.
- Cursor jitter / unexpected repeat:
  - inspect `kNavRepeatFrames`, `kStickThreshold`, `HandleNavigation`.
