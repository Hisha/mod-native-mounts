# Complete generated mount catalog report

## A. Discovery findings

Build 12340 stores all three spell effects in `Spell.dbc`; there is no separate
`SpellEffect.dbc`. `SPELL_AURA_MOUNTED` (78) identifies a mounted implementation
spell, but is not a collectible marker: the effective DBC contains test, NPC,
quest, rental, vehicle and speed-specific implementation spells with that aura.

The generator therefore admits an ordinary stock spell only when all of these
conditions hold:

1. effective `item_template` class 15/subclass 5 has a durable learn-spell slot
   (`spelltrigger_N = 6`) targeting the public spell;
2. the target exists in the effective build-12340 `Spell.dbc`;
3. that spell, or its trigger chain, resolves to aura 78.

Trainer/quest-only class mounts have no teaching item. Seven public spells are
the explicit include overlay: Felsteed, both Warhorse/Charger faction variants,
Dreadsteed, and Acherus Deathcharger. The overlay contains their native class,
faction, level and riding gates. There are no ordinary-mount registrations.

Names, icon IDs, spell levels and preview creature entries come from effective
`Spell.dbc`. The mounted effect's misc value is the creature entry used by the
server and by `PlayerModel:SetCreature`; display/model DBCs are downstream
validation data, not collectible identity. Source item, level, race, class,
profession and riding gates come from `item_template`. Riding trainer spells
33388/33391/34090/34091 resolve to 75/150/225/300. Faction equivalence comes
from `player_factionchange_spells`. Neither name, icon, creature, display nor
model is used to collapse distinct collectible spells.

Profession eligibility uses `Player::GetPureSkillValue`, so equipment, aura and
other temporary or permanent bonus points do not unlock account grants. No core
hook was added.

## B. Catalog statistics

The checked-in baseline generation reports:

- 396 mounted-aura candidates;
- 271 accepted public spell variants;
- 192 logical account collectibles;
- 125 rejected mounted-aura candidates;
- 79 faction/equivalent groups;
- 7 trainer-only include overrides;
- 0 accepted entries with an unresolved mounted-aura chain.

Exact, machine-readable rejection counts, equivalent groups and override IDs
are in `generated/mount_catalog.json`. Rejection reporting separates test/debug,
temporary/quest/rental/vehicle names, internal trigger spells, and candidates
with no durable learn source. These labels explain rejected rows; admission is
still based on the structural rule above, not the labels.

The five regression fixtures remain spell IDs 58983, 59567, 23161, 44151 and
32243. Their legacy keys are preserved. Tawny Wind Rider and Ebon Gryphon now
share the existing `tawny-wind-rider` key, preserving old ownership while
allowing the eligible faction variant.

## C. Generated artifacts

- `generated/mount_catalog.json`: canonical review artifact and statistics.
- `generated/NativeMountCatalog.inc`: compiled immutable server variants.
- `content/client/Interface/FrameXML/NativeMountCatalog.lua`: client rows
  generated from the same catalog.
- `content/mod-native-mounts.epf`: deterministic package containing the Lua
  catalog and Collections implementation.

Regenerate with `tools/generate_mount_catalog.py`, passing the effective
composed `Spell.dbc`, effective world `item_template.sql` export, and effective
`player_factionchange_spells.sql`. `--check` fails on stale outputs.

For custom content, define a normal durable mount-teaching item, a public spell
whose mounted aura chain is present in the effective patched `Spell.dbc`, and
the normal source restrictions. New/modified spell IDs, custom icon IDs,
creature entries and requirement rows are then discovered without a
mod-native-mounts entry. Run generation after Content Manager composition so it
never reads the vanilla-only snapshot. A custom trainer-only mount without a
durable teaching item is intentionally not auto-admitted because it is
indistinguishable from NPC/internal spells; such content should provide the
durable metadata convention.

## D. Code changes

`NativeMountCatalog` consumes generated variants and exposes name/icon/preview,
source and restriction metadata. `NativeMountService` retains the existing
ownership/grant tables and provenance, uses trained skill values, and sends one
event-driven ownership snapshot over the stock authenticated self-whisper addon
channel. `NativeMountModule` consumes only the `NMOUNTS` request vocabulary.
The Lua page creates nine reusable rows, scrolls over the immutable catalog,
shows uncollected and restricted states, keeps one model preview, and permits
dragging only after exact native companion-spell resolution.

No migration resets or rewrites ownership. No changes were made to transmog,
AzerothCore core, action bars, or native mount casting.

## E. Performance

Catalog construction occurs offline. Gameplay uses the immutable compiled
vector. Login keeps the existing single batched `UNION ALL` read for ownership
and character grant provenance; it performs no per-mount reads. Acquisition and
eligibility changes reconcile only affected spell/key variants. Opening the UI
performs one indexed account-ownership read, chunks the response below the
addon-message limit, and performs no repeated query while the page stays open.
Only nine row widgets and one 3-D model exist regardless of catalog size.

There is no player-update hook, timer, five-second scan, Lua `OnUpdate`, or
polling path.

## F. Verification

- Python contract/generator tests: 21 passed.
- Standalone C++ eligibility/regression binary: passed with warnings as errors.
- EPF rebuild and immediate `--check`: passed; SHA-256
  `00ebefaadd96e3d54ba0524ed95fd9f3d216d6e378a7d9b6b9aa51e83f054329`.
- Generator baseline: all 271 accepted variants resolve to a mounted aura.
- Full AzerothCore CMake configuration found the module, but the available
  checkout cannot complete generation/compilation because its MySQL include is
  `MYSQL_INCLUDE_DIR-NOTFOUND`; full worldserver compilation remains unverified.
- No live WotLK client or EPF deployment was performed. UI rendering, model
  coverage, addon transport, action-bar persistence and summoning therefore
  remain PTR validation items, not claimed results.

## G. PTR validation: Hipally and Bullsha

1. Regenerate from the exact Content Manager composed Spell DBC/world export,
   run `python3 tests/run_checks.py`, build worldserver in the deployment tree,
   and publish the rebuilt EPF through the normal Portalkeeper flow.
2. Back up the PTR character database. Do not copy changes to production.
3. Enable `NativeMounts.Poc.Enable` on PTR and restart worldserver. Confirm no
   periodic mount messages or queries appear while characters idle.
4. Log in Hipally. Open Collections > Mounts and verify 192 ordered logical rows
   backed by all 271 character/faction spell variants,
   smooth wheel/scrollbar movement, grey uncollected icons, names and previews.
5. Select known ground, flying, carpet, mammoth/vehicle-like and aquatic mounts.
   Record any missing model or poor framing; do not infer success from icons.
6. Drag a mount Hipally actually knows to a normal action bar, relog, and summon
   it. Verify the action persists and normal combat/map/riding checks still run.
7. On Hipally, inspect an account-owned Horde/class/profession-restricted mount:
   it must say collected/restricted, explain the gate, and refuse dragging when
   the exact companion spell is absent.
8. Log in Bullsha on the same account. Verify account ownership is shown from
   the server snapshot, eligible faction equivalents materialize, and ineligible
   variants do not. Repeat drag, relog and normal summon checks.
9. Learn one previously unowned native mount on either character. Without
   reopening or waiting for a timer, verify the ownership update reaches both
   online characters and only the affected logical key is reconciled.
10. Test Turbo-Charged Flying Machine below/at Engineering 375 using trained
    skill. Equip temporary skill-bonus gear below 375 and verify it does not
    grant the mount. Remove/relearn the trained skill and verify targeted revoke
    and restore preserve non-module spells.
11. Restart and recheck pre-existing five-fixture ownership rows and grant
    provenance. Confirm there was no collection reset or unrelated character
    data change.
