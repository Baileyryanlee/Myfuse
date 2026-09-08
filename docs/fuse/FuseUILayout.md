# Fuse UI layout controls

Open **Esc → Dev Tools → Fuse Debug Menu → UI Layout**. The optional pop-out debug window is useful for adjusting an open Fuse menu.

The Pause Fuse Menu section controls material-name and quantity/attack font scales, card-text offsets, equipment text scale/offsets, title position/scale, carousel position/gap, durability-bar position/size, footer/status scale and spacing, and the pause button hint position/scale. Visibility toggles cover backgrounds, icons, modifier placeholders, durability, footer/status and the button hint. Hiding a visual does not disable its gameplay input.

The Projectile Fuse Menu section controls position, font scale, visible rows, extra row spacing, panel width, backgrounds and quantities. Scrolling uses the same live row-size calculation as rendering so the selected entry remains in view.

Coordinates are pixels on the game's 320 × 240 canvas. Positive X moves right; positive Y moves down. Settings labeled as offsets are relative to their original component positions. Large text or extreme offsets can overlap or clip; each section has a reset-to-defaults button.

Material-card font scales require **Use scalable material-card font**. Other text retains its original GfxPrint font at scale 1 and uses the scalable OoT ordered font at other scales. The appearance can therefore change as well as its size. Footer/status already use the ordered font.

Changes apply on the next draw and save automatically to console-variable settings, not the game save. Reset affects only the selected layout section, including its existing Fuse font/durability display settings. Layout settings and their bounds are defined in `soh/soh/Enhancements/Fuse/UI/FuseUILayout.h`.

Manual check: move and scale a component in each menu, toggle its background, restore defaults, and restart the game to confirm settings persist. In the projectile menu, increase font size and navigate to the last entry to verify scrolling and selection.
