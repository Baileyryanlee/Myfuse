# Fuse migration to SoH 9.2.3

Baseline: `e44bd8d9897c367d9b3e853e14114cc1901839d7` (SoH 9.2.3 Ackbar Delta).
Imported Myfuse source: `c99d38f578bce844ecf0f059716a98b62aba5363` (main, March 22, 2026).

The port retains current upstream code and dependencies. It imports Fuse-owned files and selected engine integration changes instead of overwriting the source tree with the older 9.1.1 checkout. The current source globs discover the Fuse implementation and beam actor without duplicating CMake source entries.

## Compatibility changes

- Adapted actor-spawning calls and libultraship context access to current interfaces.
- Restored vanilla sword collision publication's melee-state/animation gate. Sword-beam lifetime uses attack state rather than geometry movement as its activation signal. A frame-update fallback ends an attack's beam even when sword geometry is no longer drawn.
- Allowed sword-beam endpoints to follow the blade vertically as well as horizontally.
- Validated actor-list membership before dereferencing retained actor pointers; beam cleanup also checks the custom actor identity.
- Restricted shield-bash activation to enabled Fuse runtime state.
- Kept Fuse remapping editable independently of custom ocarina controls. L remains the default when no explicit Fuse Menu mapping exists; a configured mapping takes precedence. Mapping objects are not shared with the L button.
- Reconciled frame-counter comparisons with the current unsigned engine frame counter.
- Retained current upstream shield assignment support; did not restore the obsolete duplicate enhancement file.

## Persistence and material compatibility

The material design and numeric IDs remain unchanged: None 0, Rock 1, Deku Nut 2, Frozen Shard 3, Stick 4, Bomb 5, Keese Eye 6, Fire Jelly 7, Fire Keese Eye 8, Beamos Head 9. Older material documentation that stops at Fire Jelly is incomplete.

Fuse's nested save schema remains version 5, including sword/shield slots, boomerang, hammer and separate material inventory persistence. SaveManager changes are confined to Fuse-specific initialization and registration. Use copied saves for migration tests.

## Local build

Use Visual Studio 2022, Release/x64, with the existing `build/x64/Ship.sln`. Keep the existing v143 triplet, Torch asset pipeline and dependency revisions. Do not recreate the project in another nested folder.

This sandbox required explicit MSBuild SDK location/display properties and a normalized process environment. The local build helper and detailed logs are in `build/fuse-port`. CMake uses its already-populated dependencies with `FETCHCONTENT_FULLY_DISCONNECTED=ON` and the bundled Python interpreter. Its separate stb download step can still run during configuration: with network blocked it created an empty header. The exact pinned `0bc88af4de5fb022db643c2d8e549a0927749354` header was recovered through GitHub and cached locally. This was a dependency-file repair, not a change to the submodule source or dependency version.

The test runtime is `build/fuse-test`; the original baseline runtime remains at `build/run`. Do not treat source syntax checks alone as gameplay verification. See the current local checkpoint for build and runtime results.

Validation on September 6, 2026: the complete Release/x64 build and link succeeded with 0 errors and 7 warnings. The user ran the separate test copy and reported that everything appeared to work as expected. This establishes a successful smoke test; the individual regression scenarios below have not all been independently confirmed.

## Remaining feature limits

Existing TODOs for ranged knockback, ranged-menu slowdown, hammer impact-radius expansion, shield knockback scaling and burn-duration tuning remain. This port does not implement those unfinished features. Regression testing should cover disabled Fuse, input remapping, saves, equipment, material effects, projectiles and beam lifetime across swings, breakage and scene changes.
