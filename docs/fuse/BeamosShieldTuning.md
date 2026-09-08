# Temporary Beamos shield tuning

Open **Esc → Dev Tools → Fuse Debug Menu → Beamos Tuning**. Changes save automatically to local settings. Shield and sword subsections independently tune weapons fused with Beamos Head. They do not change MaterialDef, material IDs or saved material definitions.

- **Standard shields — origin:** adult and child X/Y/Z origin offsets, plus the existing Z-target pitch-down adjustment in engine angle units (16384 = 90 degrees).
- **Standard shields — beam and combat:** enable beam, visual width, range, yaw/pitch offsets in degrees, raw damage per tick, tick interval, hit radius, forward aim threshold, durability drain amount/interval, and shield-bash boost controls.
- **Child with Hylian Shield — crouch origin and sweep:** separate X/Y/Z origin, backward compensation, lean offsets, sweep toggle and signed degrees per frame. Negative sweep speed reverses direction; disabling sweep follows the shield aim instead.
- **Child with Hylian Shield — beam and combat:** an independent copy of all beam/combat/boost controls, selected automatically while child Link guards with the Hylian Shield.
- **Diagnostics:** existing beam-tuning logging toggle.

Positions use world units: X is right, Y is up and Z is forward relative to the existing beam/body orientation. Child Hylian's effective forward offset is its Z value minus backward compensation. These control the existing laser, not a new Beamos head mesh attachment. Visual beam width and gameplay hit radius are independent.

Damage is raw engine damage per tick, not hearts or damage per second. Damage zero skips damage application; durability cost zero allows cost-free testing. Boost damage is capped at 255 to fit the engine damage field. Timers count gameplay frames, not display refresh frames. Changes to intervals/boost duration may require the pending tick or a new boost activation to take full effect. Range applies to both the displayed beam endpoint and damage checks. Existing wall-intersection and target-damage behavior remains unchanged.

Each section has its own reset button. Standard width and existing origin/sweep controls retain their old console-variable names. New child Hylian combat settings start from the original defaults independently of any customized standard settings.

Validation checklist: compare standard and child Hylian profiles; move origins and alter width; set zero damage/drain; restore defaults; test boost; reverse/disable child sweep; stop guarding or disable beam and confirm cleanup; restart to verify settings persist. This tuning interface does not certify the previously unfinished beam feature as complete.

## Swords

The sword origin/appearance subsection exposes enable, X/Y/Z offsets, visual width, range and blade following. These settings apply to all Beamos-fused swords. X follows Link's right, Y is world-up and Z offsets along the beam direction in the horizontal plane, preserving the existing origin calculation. Disabling blade following uses Link's forward direction while retaining the sword-base anchor when available.

The sword combat subsection exposes a damage multiplier, optional fixed damage override, hit radius, forward aim threshold and beam durability cost per swing. Defaults retain sword damage plus material attack bonus, one hit per enemy per swing, and four durability per beam swing. The override replaces the calculated damage; calculated damage is multiplied, rounded and capped at 255. Zero damage skips beam damage. Zero beam durability cost does not suppress ordinary melee-hit durability consumption. Drain changes apply on the next swing; damage changes do not clear an enemy's already-hit status within the current swing.

Each sword subsection resets independently and saves automatically. Existing sword offset/range/width console-variable names are retained. Manual check: test a child and adult sword, adjust position and width, toggle blade following, compare zero/fixed damage, test zero beam drain, stop swinging/disable beam to check cleanup, reset settings, then restart to check persistence. Shield profiles should remain unchanged.

### Spin attacks and charging

Sword beams support normal and large spin attacks, including quick spins. **Enable beam while charging spin attack** in the sword origin/appearance section controls the charging phase separately and defaults to on. The released spin still produces a beam when that toggle is off. The main sword-beam enable toggle controls both.

The initial slash, held charge and released spin are distinct beam attack phases. Each phase consumes the configured beam durability cost once and can hit each enemy once, rather than charging durability or repeatedly damaging the same enemy every frame while B is held. A target hit during charging can be hit again by the released spin. Canceling the charge or disabling its toggle ends the charging beam; the next phase starts with fresh hit tracking. Normal melee collision and trail gates are unchanged.

Manual regression: hold charge with the toggle on/off, release an uncharged and fully charged spin, quick-spin, cancel charging, and check beam cleanup and durability with child/adult swords. Keep blade following enabled to make the beam track the blade through the spin. Also verify that ordinary slashes and non-Beamos weapons behave normally.

## Projectile emitters

**Projectiles — arrows and slingshot** tunes the continuous forward beam emitted by Beamos-fused shots. Controls cover enable, visual width, range, raw damage per tick, hit radius and gameplay frames between ticks. Both projectile types share these settings; changing them does not change sword/shield tuning. Defaults are width 0.35, range 1600, damage 2, radius 12 and a four-frame damage interval. Zero damage provides a visual-only beam.

The firing hook snapshots whether this specific shot has Beamos Head and its Beam modifier. Later shots or changes to the queued material do not change an existing projectile's beam. The emitter starts when the projectile is released, follows its velocity (including vertical trajectory), and stops on actor impact, world impact, expiration or destruction. It is not active while the projectile is held on the bow/slingshot. Existing shot material consumption and impact effects are retained; beam ticks do not consume additional material.

The beam endpoint is clipped against static and dynamic world collision. On each damage tick, living enemy focus points within the beam radius and along the forward segment receive raw damage using the same damage-application approach as the other Fuse beams. An additional line-of-sight check rejects enemies behind solid scenery. Multiple enemies in an unobstructed beam can be damaged. This does not add beam interactions for switches, pots or other non-enemy props. Damage is suppressed if the beam actor cannot spawn or its model is not yet loaded.

Manual checks: fire Beamos arrows and pellets at enemies, terrain and empty space; aim up/down; verify that walls block both the beam and damage; confirm impact/expiration removes the beam; fire consecutive shots with different materials; disable the projectile beam; set damage to zero; and verify ordinary non-Beamos projectiles still behave normally. Check tuning persistence after restart.
