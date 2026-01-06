# Input Binding Discovery Notes

## Step 1: Input primitive references
- **SDL/controller primitives:** Only reference is the CMake download of the SDL controller database (`gamecontrollerdb.txt`) in `CMakeLists.txt`, indicating SDL controller support during build setup.
- **GLFW primitives:** `CMakeLists.txt` also requires `glfw3` on Windows, showing GLFW is part of the window/input stack for that platform.
- **ImGui binding UI:** `soh/SohGui/SohMenuSettings.cpp` contains UI entries for “Controller Bindings” and a pop-out bindings window, which appears to be the user-facing binding editor hook.

## Step 2: Binding registry / config hooks
- `soh/OTRGlobals.cpp` constructs a `std::shared_ptr<LUS::ControlDeck>` and initializes it on the global `Ship::Context`, passing a list of custom button bitmasks (e.g., `BTN_CUSTOM_MODIFIER1`, `BTN_CUSTOM_OCARINA_NOTE_*`). This looks like the central binding registry, provided by `libultraship`’s `ControlDeck`, with custom actions registered at startup.
- `soh/OTRGlobals.h` declares the custom button constants added to the control deck, which are not part of the original N64 button set.

## Step 3: Gameplay/feature binding interactions
- `soh/Enhancements/cosmetics/CosmeticsEditor.cpp` demonstrates manipulating bindings through the `ControlDeck`/`Controller` API by copying all mappings from d-pad buttons to custom ocarina buttons via `controller->GetButton(<N64 bit>)->GetAllButtonMappings()` and `AddButtonMapping(...)`. This shows how feature code works with the binding registry to define non-N64 actions.

### Not yet located
- A direct “pressed” check for a non-N64 bind (e.g., `IsPressed`-style query) was not found in the current tree. The control deck initialization and mapping manipulation above are the best entry points surfaced for adding/querying a new bind like “Fuse Menu”.
