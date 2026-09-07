# Pause Menu Fuse UI (`FusePauseBridge.cpp`)

## Repository path anchors (Shipwright-root)
- Pause bridge: `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`, `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.h`
- Pause Fuse UI window: `soh/soh/Enhancements/Fuse/UI/FuseMenuWindow.cpp`, `soh/soh/Enhancements/Fuse/UI/FuseMenuWindow.h`

This document describes the pause-menu Fuse UI bridge implemented in:

- `soh/soh/Enhancements/Fuse/UI/FusePauseBridge.cpp`

It is intentionally limited to behavior visible in this file.

## Exported bridge entry points (`extern "C"`)

The file exports four bridge functions:

1. `bool FusePause_IsModalOpen(void)`
   - Returns `sModal.open`.
2. `void FusePause_UpdateModal(PlayState* play)`
   - Handles open/close/state/input/update logic for the modal.
3. `void FusePause_DrawPrompt(PlayState* play, Gfx** polyOpaDisp, Gfx** polyXluDisp)`
   - Draws the `LB: FUSE MENU` prompt when hover context is eligible and modal is closed.
4. `void FusePause_DrawModal(PlayState* play, Gfx** polyOpaDisp, Gfx** polyXluDisp)`
   - Draws the modal and associated UI text/cards when open.

## `FuseModalState` fields

`FuseModalState` is file-local state (`static FuseModalState sModal`) and contains:

- `bool open`
  - Whether the modal is currently open.
- `int cursor`
  - Current selected material index.
- `int scroll`
  - Scroll index/state (tracked and logged; bounds updated through helpers).
- `FuseUiState uiState`
  - Current UI mode (`Locked`, `Browse`, `Preview`, `Confirm`).
- `bool isLocked`
  - Locks selection/fuse actions when current item is already fused.
- `FusePauseItem activeItem`
  - Item being fused in this modal (sword/boomerang/hammer/shields/none).
- `MaterialId highlightedMaterialId`
  - Material currently highlighted by cursor.
- `MaterialId previewMaterialId`
  - Material currently eligible for preview/confirm (`None` when locked or disabled).
- `MaterialId confirmedMaterialId`
  - Last confirmed/fused material for the active item.
- `FusePromptType promptType`
  - Prompt status (`None` or `AlreadyFused`).
- `int promptTimer`
  - Frame countdown for prompt display.
- `float carouselPos`
  - Smoothed carousel position used by renderer.
- `float carouselVel`
  - Velocity slot present in state; initialized/reset in this file.

## Open/update/close input flow

### Open hotkey behavior (edge-detected `BTN_CUSTOM_FUSE_MENU`)

- `IsFuseMenuPressed()` reads controller pad 0 and checks `BTN_CUSTOM_FUSE_MENU`.
- It edge-detects with static `sPrevDown` and returns true only on transition `down && !sPrevDown`.
- `FusePause_UpdateModal()` uses this (`fusePressed`) to attempt modal open when closed.
- Open is allowed only when `BuildPromptContext(play).shouldShowFusePrompt` is true (context built in helper defined elsewhere).
- On successful open, modal state is initialized/reset, active item resolved, and `BTN_L` is masked from `input->press.button`.

### Navigation (D-pad / stick thresholds)

When modal is open and not locked:

- Up navigation triggers on `(pressed & BTN_DUP) || (input->rel.stick_y > 30)`.
- Down navigation triggers on `(pressed & BTN_DDOWN) || (input->rel.stick_y < -30)`.
- Movement uses `MoveCursor(...)` (defined in this file).
- State is set to `Preview` after movement.

### `A` / `B` / `Start` behavior (including Confirm state)

- `Start` (`BTN_START`): closes modal immediately, clears relevant input fields, returns.
- `B` (`BTN_B`):
  - If current state is `Confirm` and not locked: cancel confirm by returning to `Preview` and consume `BTN_B` from `press.button`.
  - Otherwise: close modal, clear/suppress input fields, return.
- `A` (`BTN_A`):
  - If locked: triggers `AlreadyFused` prompt for 60 frames.
  - If preview material is `None`: no action.
  - If not already in `Confirm`: transitions to `Confirm`.
  - If already in `Confirm`: executes fuse operation for active item and material (`Fuse::TryFuseHammer`, `Fuse::TryFuseBoomerang`, `Fuse::TryFuseSword`, or `TryFuseShield`), then:
    - success: sets locked state, stores confirmed material, clears preview, moves to `Locked`.
    - failure: returns to `Preview`.

### Input suppression (zeroed/masked fields)

At end of normal open-modal update path (`FusePause_UpdateModal`):

- Zeroed:
  - `input->press.button = 0`
  - `input->press.stick_x = 0`
  - `input->press.stick_y = 0`
  - `input->rel.stick_x = 0`
  - `input->rel.stick_y = 0`
- Masked off in `input->cur.button`:
  - `BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_L | BTN_R | BTN_Z | BTN_CUP | BTN_CDOWN | BTN_CLEFT | BTN_CRIGHT`

Additional close paths also clear/mask subsets:

- On `Start`: masks `BTN_B | BTN_START` and zeros press/rel stick fields.
- On close via `B`: masks `BTN_B` and zeros press/rel stick fields.
- On successful modal open: clears `BTN_L` from `input->press.button`.

## Render flow

### `FusePause_DrawPrompt` behavior

`FusePause_DrawPrompt(...)`:

- Early-outs on invalid pointers.
- Builds prompt context via `BuildPromptContext(play)` (defined elsewhere).
- If modal is open, returns (prompt hidden while modal is active).
- If `context.shouldShowFusePrompt` is false, returns.
- Otherwise restores pause text state and prints `LB: FUSE MENU` at `(238, 196)` using `GfxPrint`.

### `FusePause_DrawModal` early-outs

`FusePause_DrawModal(...)` returns early when:

1. Required pointers are null.
2. Pause is not open (`!context.isPauseOpen`): also forces `sModal.open = false`.
3. Modal is not open.
4. `pauseCtx->state != 6`: also forces `sModal.open = false`.
5. Active pause page no longer matches item (`!IsPausePageForItem(...)`): also forces `sModal.open = false`.
6. Same-frame duplicate draw guard (`sLastModalFrame == currentFrame`): draws a magenta full-screen proof overlay and returns.

### Footer prompt/status strings behavior

In `FusePause_DrawModal` footer block:

- Prompt line defaults to: `A: Select   B: Back`
- If locked: `B: Back`
- If confirm mode: `A: Confirm   B: Cancel`
- Status line `ITEM ALREADY FUSED` is drawn when:
  - modal is locked, or
  - `promptTimer > 0` and `promptType == FusePromptType::AlreadyFused`

Footer text uses ordered-font rendering helpers (`FuseUi_DrawOrderedTextTrackedWithColor`, defined in this file).

### Scissor usage (`SetScissorRect` / fullscreen)

File-local helpers:

- `SetScissorRect(...)`: clamps requested rect to screen bounds and applies `gDPSetScissor`.
- `SetScissorFullscreen(...)`: applies full-screen scissor `(0,0)-(320,240)`.

Observed modal draw usage:

- Resets OPA and XLU to fullscreen early in `FusePause_DrawModal`.
- Uses `SetScissorRect(...)` for right-card carousel clipping before drawing material cards.
- Restores fullscreen scissor after carousel.
- Uses `SetScissorRect(...)` for left text/durability area while drawing durability bar and left-side text.
- Restores fullscreen scissor after each clipped section and at function end.

## CVars referenced in this file

All names below are exact strings passed through `CVAR_DEVELOPER_TOOLS(...)` constants in this file.

| CVar name | Type from getter | Fallback default | Where used |
|---|---:|---:|---|
| `Fuse.DurabilityBarEnabled` | int (`CVarGetInteger`) | `1` | `IsDurabilityBarEnabled()`; consumed by `FusePause_DrawModal()` to gate durability bar draw |
| `Fuse.UiPauseCardNameScale` | float (`CVarGetFloat` via `ReadScaleFloat`) | `1.0f` | `DrawMaterialCard()` |
| `Fuse.UiPauseCardQtyScale` | float (`CVarGetFloat` via `ReadScaleFloat`) | `0.70f` | `DrawMaterialCard()` |
| `Fuse.UiPauseFooterPromptScale` | float (`CVarGetFloat` via `ReadScaleFloat`) | `0.90f` | `FusePause_DrawModal()` footer prompt scale |
| `Fuse.UiPauseFooterStatusScale` | float (`CVarGetFloat` via `ReadScaleFloat`) | `0.85f` | `FusePause_DrawModal()` footer status scale |
| `Fuse.UiPauseUseOrderedFont` | int (`CVarGetInteger`) | `1` | `DrawMaterialCard()` ordered-font on/off |
| `Fuse.Pause.OrderedTighten` | float (`CVarGetFloat`) | `0.75f` | `DrawMaterialCard()` tracking clamp; `FusePause_DrawModal()` footer tracking clamp |

## Referenced helpers/symbols defined elsewhere

This file calls/uses external helpers and systems that are not defined here, including:

- Prompt/context and pause helpers: `BuildPromptContext`, `PauseItemSlotId`.
- Fuse operations/data: `Fuse::TryFuseHammer`, `Fuse::TryFuseBoomerang`, `Fuse::TryFuseSword`, `TryFuseShield`, `Fuse::GetMaterialDefs`, `Fuse::GetMaterialCount`, `Fuse::Get*Slot*` accessors.
- Modal list/layout helpers in this translation unit but not exported: `MoveCursor`, `ComputeVisibleRows`, `UpdateModalBounds`, etc.

These are referenced by name only; behavior beyond visible call sites is defined elsewhere.

## Debug Signals (log tags present)

`Fuse::Log` strings in this file include these tags/prefixes:

- `[FuseDBG]`
- `[FuseDBG_UI]`
- `[FuseMVP]` (commented-out log line)
- `[FuseUI]` (commented-out log line)
