# Event-driven synchronization refactor

Date: 2026-11-15  
Core inspected: AzerothCore `32fd3472aa74961c91cb9bc942ee3e0a44b36779`

## Outcome

Mount synchronization now runs only at login or after a relevant ownership,
level, riding, or profession event. The player-update hook, five-second elapsed
state, and `NativeMounts.Poc.ReconcileIntervalMs` setting were removed. An idle
online character has no module callback that can reconcile and therefore
generates zero mount reconciliation queries.

The account ownership and per-character native-spell architecture is unchanged.
The catalog remains the same five entries. Grants still commit pending
provenance before `learnSpell`; revocation still requires active/pending module
provenance. Independent spells and state-4 external-unlearn tombstones remain
protected.

## Event-to-handler mapping

| Change | AzerothCore event | Module handling |
|---|---|---|
| Login/relog | `PLAYERHOOK_ON_LOGIN` / `OnPlayerLogin` | One batched ownership+ledger read for all five keys; imports independent known spells, evaluates eligibility, grants/revokes, and publishes any newly imported ownership. |
| Recognized mount learned | `PLAYERHOOK_ON_LEARN_SPELL` / `OnPlayerLearnSpell` | Ignores guarded module learns; inserts ownership idempotently, marks the source spell independent, and reconciles only that logical key for registered same-account online characters. |
| Recognized mount forgotten | `PLAYERHOOK_ON_FORGOT_SPELL` / `OnPlayerForgotSpell` | Ignores guarded module revokes. An eligible external forget writes suppression state 4. A core forget caused after loss of skill eligibility converts active module provenance to state 2. |
| Level increase/decrease | `PLAYERHOOK_ON_LEVEL_CHANGED` / `OnPlayerLevelChanged` | Selects only variants whose minimum-level threshold lies between old and new levels. No crossed threshold means no query. |
| Skill gain through use | `PLAYERHOOK_ON_UPDATE_SKILL` / `OnPlayerUpdateSkill` | Post-change hook. Riding and catalog profession thresholds are checked; unrelated gains do nothing. |
| Skill set/decrease/unlearn/add | `PLAYERHOOK_ON_SET_SKILL` / `OnPlayerSetSkill` | Post-change hook receives old and new values. Handles riding loss, profession loss/unlearn, and direct GM skill changes by crossed threshold. |
| Race/faction/class service | Offline character-service transaction in this core | There is no safely usable live character mutation event covering all three. The character is offline; the required login reconciliation sees the final race/class/faction. No polling is added. |
| Administrative ownership grant/revoke | `.native-mounts ownership grant <account> <key>` and `revoke` | Upserts owned state or durable state-2 ownership tombstone, then reconciles only that key for online characters. Offline characters reconcile at login. |
| Collections state | Stock learned/unlearned spell packets plus client `SPELLS_CHANGED` | The open Mounts page refreshes its rows; opening the page always refreshes from the native spellbook. |

`OnPlayerUpdateSkill` is a skill-change event despite its name; it is not the
per-tick `OnPlayerUpdate` hook.

## Database query-count comparison

Counts below are reconciliation reads; writes occur only for actual ownership or
spell/provenance transitions.

| Scenario | Before | After |
|---|---:|---:|
| Idle online character | 11 reads every 5 seconds with no known catalog spells; up to 16 with five known spells | 0 |
| Login, no state changes | 11-16 reads | 1 batched `UNION ALL` read |
| Level change crossing no catalog threshold | Full reconciliation, 11-16 reads | 0 |
| Level/skill change crossing a threshold | Full five-entry reconciliation, 11-16 reads | 1 batched read restricted by `mount_key IN (...)` |
| One mount acquisition | Ownership write, then eventual full scans by every online character | 1 ownership-state read plus one targeted batched read per affected online character; no full-account scan |
| Administrative change | Depended on the next timer/login | 1 targeted batched read per affected online character; offline cost deferred to login |

The former pass issued one account ownership query, two per-entry grant/suppress
queries, and (for each already known spell) another provenance query. The new
state loader combines ownership and provenance with one indexed query and limits
event-driven work to affected keys. Non-order-critical ledger state updates use
the asynchronous database queue. Synchronous writes remain only where durability
or ordering is correctness-critical: ownership publication, pending grant
provenance before `learnSpell`, administrative tombstones, and external-unlearn
suppression.

## Automated verification

Run:

```bash
python3 tests/run_checks.py
```

Coverage includes the eligibility matrix, cross-faction denial, class and
profession restrictions, upward and downward level/riding/profession threshold
selection, unrelated-change no-op behavior, independent-spell removal guards,
external-unlearn suppression, batched login loading, same-account key fan-out,
administrative tombstones, client refresh, and the absence of a timer/player
update reconciliation path.

All module translation units also compile individually against the core revision
listed above. A full `modules` target in this environment stops in AzerothCore's
database library before the module link because the external MySQL headers lack
`mysql/udf_registration_types.h`.

## Limitations requiring integration rather than polling

- AzerothCore does not fire `OnPlayerLearnSpell` when an already-active module
  spell is independently acquired again. A reviewed item/trainer/quest
  acquisition-evidence adapter, or an explicit administrative “adopt as
  independent” operation, remains the smallest safe integration.
- Raw administrative SQL cannot notify a running worldserver. Administrative
  tools must call `Service::SetAccountOwnership` (the supplied commands do), or
  a deployment with external writers must add a small invalidation message/API.
- Multiple worldserver processes sharing one character database need an
  inter-process ownership event bus. This module synchronizes online characters
  in the current worldserver and deliberately does not poll the database.
- `Player::ModifySkillBonus` has no AzerothCore PlayerScript hook at the inspected
  revision. Base riding/profession value changes are covered, but an online
  temporary or equipment skill bonus crossing an eligibility threshold cannot
  be observed completely from a module (equip hooks do not cover arbitrary aura
  expiry). The smallest safe core integration is one post-change
  `OnPlayerSkillBonusChanged(player, skillId, oldEffective, newEffective)` hook
  emitted by `ModifySkillBonus`; this module can route it through the existing
  threshold handler. Polling was not added as a substitute.

## PTR regression procedure

Use a disposable PTR character database and build-12340 client only.

1. Apply the module SQL, install the regenerated EPF, enable
   `NativeMounts.Poc.Enable`, and confirm the removed interval setting is absent.
2. Enable database query logging for the two `native_mount_*` tables. Leave a
   character idle for at least 30 seconds and confirm zero queries after login
   settles.
3. Learn each fixture through its normal acquisition path. Confirm one durable
   account row, no duplicate rows, no recursive learn handling, native spellbook
   presence, and immediate Collections refresh.
4. With supported same-account concurrent sessions, keep another eligible and
   ineligible character online. Confirm only eligible characters receive the
   spell and that Collections refreshes. If the realm forbids concurrent
   same-account sessions, verify the offline character at its next login and do
   not claim concurrent-session validation.
5. Exercise Human/Orc and Paladin/Warlock cases for Tawny Wind Rider and
   Dreadsteed. Confirm independent spells are never removed.
6. Cross Engineering 375 in both directions, unlearn/relearn Engineering, and
   cross riding 75/150/225/300 in both directions. Confirm only affected mounts
   change and no suppression loop is created by core skill removal.
   If the proposed skill-bonus hook is installed, also cross Engineering 375 by
   equipping/removing a bonus item and by applying/expiring a bonus aura.
7. Cross every catalog level threshold upward and downward. Confirm only mounts
   at the crossed threshold are queried/changed.
8. Externally unlearn an eligible module grant. Confirm state 4 prevents regrant.
   Independently learn a fixture and confirm later eligibility/admin revocation
   does not remove it.
9. Run `.native-mounts ownership revoke <account> <key>` and `grant`; verify
   immediate online changes, durable ownership tombstones, and next-login
   handling for offline characters.
10. Delete the collecting character and verify account ownership and another
    character's mount survive. Exercise normal cast, action-bar drag, cooldown,
    combat/location/flying restrictions, relog, and restart behavior.

No running-client PTR validation was performed as part of this repository
change; do not promote the PTR items above to verified without captured client
and server evidence.
