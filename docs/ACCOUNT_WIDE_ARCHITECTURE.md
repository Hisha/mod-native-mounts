# Account-wide mount architecture and proof of concept

Date: 2026-11-15  
Target: AzerothCore WotLK 3.3.5a, client build 12340  
Status: architecture accepted for PTR proof; full Mount Journal is out of scope

## 1. Evidence labels and outcome

- **Source-verified**: established in the checked-out AzerothCore, module, SQL, or build-12340 client data/source.
- **Automated**: exercised by repository tests or compilation.
- **Experimental**: supplied server experiment, not reproduced by this repository.
- **PTR-required**: requires a running build-12340 client/worldserver and is not claimed as verified.
- **Proposed**: production design beyond the intentionally small proof of concept.

The selected architecture is **account ownership plus character-specific native
spell grants**. Ownership is a durable account record. Usability is recomputed
for each character. An eligible character is taught the ordinary public mount
spell, so the stock spellbook, action bars, tooltips, cooldowns, persistence, and
cast validation remain the integration surface.

The previous journal-only cast recommendation is rejected for the confirmed
product requirement: an unlearned spell cannot be installed as a durable native
spell action. A learned native spell can. The PoC therefore tests materialized
grants with a provenance ledger.

No AzerothCore core patch is used. No live data is touched. The PoC is disabled
by default and contains five reviewed fixtures rather than a complete catalog.

## 2. Verified source findings that drive the design

1. `Player::learnSpell` stores permanent spells in the normal player spell map,
   sends the learn packet, invokes `OnPlayerLearnSpell`, and is persisted through
   `character_spell`. **Source-verified:** `Player.cpp:3428-3459`,
   `PlayerStorage.cpp:6640-6664,7894-7934`.
2. `Player::removeSpell` invokes `OnPlayerForgotSpell` and sends the unlearn
   packet. **Source-verified:** `Player.cpp:3513-3664`.
3. Modules can observe login, update, level, learn, forget, and logout without a
   core change. **Source-verified:** `PlayerScript.h` hook definitions and
   `PlayerScript.cpp` dispatch.
4. A spell action is accepted only when `Player::HasSpell(action)` and is stored
   in the normal character action table. Invalid spell actions are marked for
   deletion while actions load. **Source-verified:**
   `Player.cpp:5745-5829`, `PlayerStorage.cpp:5799-5822,7335-7375`.
5. Learning mount spells updates normal learn-spell and skill-line achievement
   criteria; mount-count criteria scan the character spell map. Consequently,
   materialized account grants affect character mount achievements.
   **Source-verified:** `Player.cpp:3406-3414`, `AchievementMgr.cpp:1539-1561`.
6. Source item restrictions include level, riding, race, class, required spell,
   and profession. These are acquisition checks, not a universal learned-spell
   cast policy. **Source-verified:** `Player::CanUseItem` and build-12340
   `item_template`; **experimental:** Dreadsteed and Tawny Wind Rider worked on
   an Alliance Paladin after `.learn`, while Turbo Flying Machine dynamically
   enforced Engineering.
7. Normal, non-triggered mount casting retains combat, movement, map, instance,
   battlefield, no-fly, Cold Weather Flying, form, water, and spell-script
   checks. The module does not use direct `Unit::Mount` or triggered-ignore
   flags. **Source-verified:** `Spell::CheckCast`, `SpellInfo::CheckLocation`,
   and `spell_gen_mount` paths cited in the original investigation.
8. Build 12340 FrameXML uses the pre-4.0 spellbook-slot form of `PickupSpell(slot, BOOKTYPE_SPELL)`.
   The PoC resolves the learned spell's book slot and places that native spell
   payload on the cursor. **Source-supported; PTR-required** for the complete
   drag/drop interaction in the patched client.

## 3. Ownership, import, variants, and reconciliation

### 3.1 Identities

- `mount_key` is a stable logical collectible identity.
- One logical key may have multiple faction/race variants in the production
  catalog.
- `spell_id` is the character grant and native action identity.
- Account ownership never depends on the continued existence of the original
  character.

For faction pairs, one account key owns the pair and the resolver selects only a
variant allowed for the current race/team. `player_factionchange_spells` is a
strong seed for pair discovery, but the production catalog remains reviewed.
The PoC deliberately does not map Tawny Wind Rider to an Alliance mount; an
Alliance character receives nothing.

### 3.2 Acquisition and backfill

Production reconciliation is idempotent:

1. On a non-module `OnPlayerLearnSpell`, map the spell to its logical key and
   `INSERT IGNORE` account ownership.
2. On login, import recognized spells already in the character spell map when
   no active module-ledger row proves they were module grants.
3. Before production rollout, run an offline resumable backfill joining
   `characters(account)` to `character_spell`, mapping recognized spells to
   keys, and inserting distinct account rows.
4. Account ownership is monotonic for ordinary forget events. Only an explicit
   administrative account revoke changes ownership to a durable revoked
   tombstone. The tombstone prevents login/import from immediately recreating it.

The PoC implements steps 1 and 2. It intentionally omits an offline migration
command because its catalog is not production-complete.

### 3.3 Grant ledger state machine

| State | Meaning | May the module remove the spell? |
|---|---|---|
| pending | Ledger committed before `learnSpell`; crash-safe retry point | Yes, after it becomes active |
| active | Module introduced the currently known spell | Yes, on ownership revoke or ineligibility |
| module-revoked | Module removed its own grant | No spell remains; eligible reconciliation may grant again |
| independent | A non-module learn was observed | Never |
| external-unlearn suppression | A non-module forget removed the spell | No; prevents a grant/forget loop |

Grant order is ledger pending → `Player::learnSpell` → active. Durable provenance
is written before the spell becomes visible. A failed learn remains retryable.
Revoke requires all of: an active/pending row, `introduced_by_module=1`, and a
current loss of ownership or eligibility. A known spell without that evidence
is never removed.

`Player::learnSpell` rejects an already-active spell before firing the learn
hook. Therefore the core cannot reveal that a character independently consumed
an acquisition source for an identical spell while a module grant was already
active. Production must add acquisition-evidence adapters for reviewed mount
items/trainers/quests, or expose an explicit “adopt as independent” administrative
operation. Until that exists, revoke is conservative but cannot prove this rare
transition. This is an unresolved limitation, not silently treated as solved.

### 3.4 Eligibility-change detection

- Login reconciliation handles offline level, race/faction, class, riding, and
  profession changes.
- `OnPlayerLevelChanged` handles increases and decreases and selects only catalog
  entries whose level threshold was crossed.
- `OnPlayerUpdateSkill` handles successful riding/profession gains;
  `OnPlayerSetSkill` handles direct increases, decreases, additions, and
  unlearning. Both select only crossed catalog thresholds.
- `OnPlayerLearnSpell` publishes newly inserted ownership to the module's online
  same-account character registry and reconciles only that logical mount key.
- Administrative ownership commands publish the same targeted invalidation.
- There is no player-update hook, timer, periodic database query, or idle
  reconciliation path.

Grant revocation sends the stock unlearn packet. Core does not eagerly delete an
existing action row at `removeSpell`; on the next login, action validation rejects
the unknown spell and deletes that button. The exact immediate UI appearance and
whether a re-grant can preserve the button without a relog are **PTR-required**.

## 4. Character eligibility matrix

| Restriction | Grant-time policy | Cast-time policy | PoC fixture/evidence |
|---|---|---|---|
| Character level | Require catalog minimum | Native spell checks still apply | Big Bear 20; Dreadsteed 40; Tawny 60; flyers 70 |
| Riding skill | Require source-equivalent rank | Native/adaptive scripts still apply | 75/150/225/300 fixtures; automated |
| Faction/race | Require exact reviewed race mask; resolve equivalents by key | Do not rely on generic cast | Tawny mask 690 denies Human, allows Orc; automated and experimental risk demonstrated |
| Class | Require reviewed class mask | Do not rely on generic cast | Dreadsteed Warlock-only; automated and experimental risk demonstrated |
| Profession | Require skill and rank | Keep native dynamic check | Turbo Engineering 375; automated; experimental dynamic cast check |
| Flying permission | Grant a legitimate flying spell only after riding gate | Native map/area/instance/no-fly checks | Azure/Turbo; source-verified, PTR-required end to end |
| Cold Weather Flying | Do not make ownership location-dependent | Native Northrend location check | Source-verified, PTR-required |
| Combat/movement/form/water | Not an ownership/grant property | Native non-triggered cast | Source-verified, PTR-required |
| Faction change | Re-resolve logical variant; revoke only module variant | Native cast is insufficient | Production catalog design; Tawny denial PoC |
| Independently known spell | Import ownership; create no removable module provenance | Ordinary native behavior | Service contract test; PTR-required persistence proof |

## 5. Database design

The implemented PoC schema is
[`001_native_mounts_poc.sql`](../data/sql/db-characters/base/001_native_mounts_poc.sql).
It contains:

- `native_mount_account_ownership(account_id, mount_key, provenance...)`;
- `native_mount_character_grant(character_guid, spell_id, mount_key, state,
  introduced_by_module, timestamps)`;
- `native_mount_favorite(account_id, mount_key, created_at)` reserved for the
  next phase.

There is deliberately no foreign key from a character DB to the auth DB and no
foreign key to a generated catalog. Account deletion needs an explicit purge
operation. Character deletion must not cascade ownership; it may delete only
that character's ledger rows. Account ID reuse remains an operational risk
unless auth supplies a non-reused account incarnation.

Administrative grant inserts ownership or changes a revoked tombstone back to
owned, followed by cache invalidation/reconciliation. Administrative revoke
changes ownership state to the revoked tombstone, followed by reconciliation of
all online same-account characters; offline module-ledger grants are removed at
next login. Independently known spells are untouched.

Account-wide means characters sharing this configured collection database.
Independent character databases do not become cross-realm collections.

## 6. Native action-bar assessment

The recommended and implemented PoC path is:

```text
account ownership → eligible native learned spell → spellbook slot
        → Collections Random Mount PickupSpell(slot, BOOKTYPE_SPELL)
        → ordinary ACTION_BUTTON_SPELL → normal cast validation
```

Expected from source, but still requiring live client validation:

- drag a collected row to stock and popular replacement action bars;
- native spell tooltip, range/usability tint, cooldown, and error text;
- cast success and failure through the ordinary client opcode path;
- persistence after logout and worldserver restart;
- relog behavior after eligibility revoke and after subsequent re-grant;
- action survival when an active module grant is promoted to independent.

The PoC page is a sibling `nativeCollectionPages` contributor with ID `mounts`,
owns only `NativeMountsFrame.xml/.lua`, and is packaged through EPF. It does not
modify Content Manager or Native Transmog. Its creature-only previews use
`PlayerModel:SetCreature` and include uncollected fixtures. This is code-complete
but **not client-verified**.

## 7. Random Favorite Mount recommendation

Use one allocated custom public spell, “Random Favorite Mount,” delivered in the
cumulative client/server DBC patch and taught as a stable utility spell. It must
not belong to skill line 777 and therefore must not count as a mount achievement.
Its icon can be dragged by the same native spellbook-slot path and its action row
persists normally.

On a non-triggered cast, the server:

1. loads account favorites;
2. intersects favorites with owned logical keys;
3. resolves a character variant and static eligibility;
4. chooses uniformly using the server RNG;
5. initiates the selected public mount as a normal, non-triggered cast;
6. lets native dynamic cast checks accept or reject it.

To avoid choosing a favorite that is statically eligible but currently blocked,
production should preflight the same normal cast checks where the spell API
allows it, retry among remaining favorites only for deterministic usability
failures, and never use triggered-ignore masks. The final selected cast remains
authoritative. Favorites are account rows and are never trusted from Lua.

A macro/addon-only button is not recommended: macro slots are user-managed,
addon code is not authoritative, and macro deletion/renaming weakens persistence.
An existing unrelated spell ID must not be repurposed. Custom spell ID allocation
and DBC publication must go through the established content pipeline. This phase
is not implemented by the PoC.

## 8. Achievements and legacy interface

Because the selected design teaches real spells, stock per-character mount-count
achievements will count eligible account grants. This differs from original
WotLK acquisition semantics but follows from the requirement for functional
native action buttons. Do not patch achievements in the PoC. PTR must measure
learn criteria, earned achievements, revoke behavior, and whether re-granting
causes duplicate notifications (the criteria count itself scans unique spells).

Do not remove the Character Info Pets page during the PoC. After the journal is
feature-complete, hide or redirect only its mount subview through patched
FrameXML. Preserve companion-pet tabs and do not touch hunter/warlock class-pet
systems. Exact frame ownership/event behavior requires a separate client audit
and PTR regression pass.

## 9. Proof-of-concept contents and automated evidence

Implemented:

- five-entry reviewed catalog: Big Blizzard Bear `58983`, Azure Drake `59567`,
  Dreadsteed `23161`, Turbo Flying Machine `44151`, Tawny Wind Rider `32243`;
- account ownership import on learn and login;
- eligible login and event-driven grants through `Player::learnSpell`;
- module-only revoke through `Player::removeSpell`;
- provenance ledger and external-unlearn suppression;
- independent `mounts` Collections registration;
- native spellbook drag payload and creature-only preview shell;
- deterministic EPF artifact.

Automated results:

- ten Python contract tests pass;
- standalone C++ eligibility matrix passes with warnings-as-errors;
- all five AzerothCore module translation units compile against revision
  `32fd3472aa74961c91cb9bc942ee3e0a44b36779`;
- deterministic EPF `--check` passes.

The full local AzerothCore `modules` target was not completed because the local
external MySQL header bundle lacks `mysql/udf_registration_types.h`. This occurs
while compiling AzerothCore's database library, before linking this module. It is
an environment dependency issue; translation-unit compilation still proves the
module's checked APIs and signatures.

## 10. PTR test procedure

Use a disposable PTR character database only.

1. Install the module, apply the PoC character SQL, compose/install the EPF with
   Content Manager, and set `NativeMounts.Poc.Enable = 1`.
2. Create Character A and eligible/ineligible same-account Character B/C cases.
   Record baseline `character_spell`, `character_action`, ownership, and ledger
   rows.
3. On A, acquire/learn Big Blizzard Bear normally. Confirm one account row is
   created. Do not use `.learn` for the success-path proof.
4. Log B in with level 20/riding 75. Confirm spell `58983` is learned, ledger is
   active, normal cast works, Collections drag works, and the action persists
   after relog and worldserver restart.
5. Repeat Azure Drake at 70/riding 300. Test Outland, Northrend with/without Cold
   Weather Flying, no-fly areas, instances, water, combat, movement, forms, and
   already-mounted state.
6. Prove Dreadsteed is granted to an eligible Warlock but not a Paladin; Tawny
   Wind Rider to an allowed Horde race but not an Alliance race; Turbo only at
   Engineering 375/riding 300/level 70. Lower Engineering to 1 and observe the
   immediate event-driven revoke and action-bar behavior.
7. Independently learn one fixture on B before account reconciliation. Confirm
   ownership import occurs, no removable module row is created, and later
   eligibility/account changes do not remove the spell.
8. Externally unlearn a module grant. Confirm the state-4 suppression tombstone
   prevents a regrant loop. Clear only the test row to resume reconciliation.
9. Delete A. Confirm account ownership and B's function survive. Delete only PTR
   data created for the test after retaining logs/screenshots.
10. Capture client build number, server commit/config, SQL before/after snapshots,
    video/screenshots of drag/cast/relog, and exact errors. Only then promote the
    corresponding PTR-required findings to verified.

## 11. Unresolved limitations

1. No running-client verification has been performed in this worktree.
2. Full catalog identity and source-equivalent eligibility still require curated
   data generation and review.
3. Independent acquisition while an identical module grant is already active is
   not observable through `OnPlayerLearnSpell`; acquisition evidence is needed.
4. Granting real account mounts changes per-character achievement counts.
5. Removing a granted spell can cause core to delete its action row at next
   login; re-grant does not promise automatic button restoration.
6. Custom Random Favorite spell allocation, DBC data, server handler, favorites
   service, and PTR validation are not implemented.
7. Account deletion/ID reuse and multi-worldserver invalidation need deployment-
   specific operations.
8. Cross-realm sharing exists only when realms deliberately share the collection
   database/service.
9. Faction-equivalent mappings beyond the five PoC fixtures are not implemented.
10. Safe replacement of the legacy mount UI needs a separate FrameXML audit.
11. Model scale/position presets and unseen-model behavior need live client work.
12. Direct SQL edits made outside the module's administrative API cannot emit an
    online invalidation event. Use the supplied commands; offline characters
    still reconcile on login.

## 12. Go/no-go gate

Proceed to the full Mount Journal only after PTR demonstrates that Character B
can cast Character A's mount through a persisted ordinary action button, all five
restriction fixtures behave as specified, and an independently learned spell is
never removed. If native spell dragging or persistence fails in build 12340,
stop and document that limitation before considering any custom action system.
If provenance cannot be made safe for the production catalog, fall back to
character-specific collections rather than silently weakening restrictions or
removing independent spells.
