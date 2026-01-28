# Fuse Projectile + Bomb Behavior Report

## Executive Summary
- Projectile collision against world geometry is still driven by the vanilla projectile actors (En_Arrow and Arms_Hook); Fuse only adds hit callbacks and does not change collider registration or `BgCheck_*` calls.
- Fuse-triggered explosions are spawned via `Fuse_TriggerExplosion` (EnBom actor) and marked using `actor.home.rot.z` to enforce single-hit AT in `EnBom_Explode`.
- The only direct Fuse edit inside `z_en_bom.c` is the Fuse marker + single-hit AT path; any vanilla bomb regressions likely stem from this marker repurposing `home.rot` fields or unintended propagation of the marker.

## Diff Map (Fuse Intersections)
- **Projectile hit hooks:** `EnArrow` and `ArmsHook` call `FuseHooks_*` on projectile fire and on hit (actor or surface). These functions eventually call `Fuse_OnRangedHitActor` or `HandleRangedSurfaceHit` to decide whether to trigger explosions or other modifiers.
- **Explosion spawn:** `Fuse_TriggerExplosion` spawns an `ACTOR_EN_BOM` and forces it into `BOMB_EXPLOSION` mode with a Fuse marker.
- **Bomb explosion behavior:** `EnBom_Explode` checks the Fuse marker and suppresses repeated AT damage for marked explosions.

## Next Steps (Suggested Fix Strategy)
- Restore vanilla collision and bomb damage behavior by removing or isolating Fuse marker usage in `EnBom_Explode`.
- Re-implement Fuse explosions in a separate actor or a dedicated code path that does not reuse `EnBom` state fields shared by vanilla bombs.

