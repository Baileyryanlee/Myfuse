# FuseMaterials Implementation Reference

Related docs: [FuseArchitectureOverview](./FuseArchitectureOverview.md), [FuseCoreSystem](./FuseCoreSystem.md), [FuseModifiers](./FuseModifiers.md), [FuseState](./FuseState.md).

## Repository path anchors (Shipwright-root)
- Materials table/types: `soh/soh/Enhancements/Fuse/FuseMaterials.cpp`, `soh/soh/Enhancements/Fuse/FuseMaterials.h`

## Purpose / scope
- `FuseMaterials` owns the static material registry (`kMaterialDefs`) and lookup functions used by Fuse systems.

## Material registry and enumeration
- Material IDs are declared in `MaterialId` enum:
  - `None(0), Rock(1), DekuNut(2), FrozenShard(3), Stick(4), Bomb(5), KeeseEye(6), FireJelly(7)`.
- Registry lives in `constexpr MaterialDef kMaterialDefs[]`.
- Registry iteration and exposure:
  - `FuseMaterials::GetMaterialDefs(size_t* count)` returns array pointer and optional count.
- Single lookup:
  - `FuseMaterials::GetMaterialDef(MaterialId id)` linear-searches by `def.id`.

## Material ID stability warning
- IDs are persisted in save data (`matId`) and used across C/C++ bridge enums.
- Treat numeric values as stable ABI/save identifiers.
- Renumbering or reusing IDs can corrupt old saves or break C-side callers.

## Per-material attributes (as declared)
- `MaterialDef` fields:
  - `id`
  - `name`
  - `attackBonus`
  - `baseMaxDurability`
  - `modifiers` pointer + `modifierCount`

Current table from `kMaterialDefs`:
- `None`: name `"None"`, atk `0`, base durability `0`, no modifiers.
- `Rock`: name `"ROCK"`, atk `1`, base durability `10`, mods: `Hammerize(1), Knockback(1), PoundUp(1), NegateKnockback(1)`.
- `DekuNut`: name `"Deku Nut"`, atk `0`, base durability `5`, mods: `Stun(1), MegaStun(1)`.
- `Stick`: name `"Stick"`, atk `2`, base durability `3`, mods: `RangeUp(3), WideRange(3)`.
- `FrozenShard`: name `"Frozen Shard"`, atk `0`, base durability `8`, mods: `Freeze(1)`.
- `Bomb`: name `"Bomb"`, atk `1`, base durability `1`, mods: `Explosion(1)`.
- `KeeseEye`: name `"Keese Eye"`, atk `0`, base durability `4`, mods: `Seek(1)`.
- `FireJelly`: name `"Fire Jelly"`, atk `0`, base durability `10`, mods: `Burn(1)`.

## UI-facing metadata in this file
- Name strings are provided by `MaterialDef::name`.
- No icons/object IDs are defined in this file.
- TODO (verify): inspect UI/rendering symbols for icon/object mapping if needed.

## Key query helpers used by rest of system
- `FuseMaterials::GetMaterialDef(MaterialId id)`.
- `FuseMaterials::GetMaterialDefs(size_t* count)`.

## CVars referenced in this file
- None.

## Debug log tags/prefixes in this file
- None.

## Common mistakes / regression notes
- Do not reorder/renumber `MaterialId` values unless save migration + C bridge updates are implemented.
- Keep `kMaterialDefs` coverage consistent with `MaterialId` enum and bridge enum (`FuseMaterialId`).
- Ensure `modifierCount` matches the static modifier array length for each entry.
