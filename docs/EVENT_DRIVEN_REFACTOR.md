# On-demand Collections reconciliation

Date: 2026-11-15  
Target: AzerothCore WotLK 3.3.5a (build 12340)

## Root cause and architecture

The generated catalog expanded login reconciliation from five fixtures to 271
spell variants (192 logical mounts). Every login loaded ownership and grant
provenance, inspected the complete catalog and spellbook, wrote imports,
granted/revoked spells, published snapshots, and could reconcile other online
characters on the account. Catalog size and playerbot population therefore
became direct multipliers of login work.

The module no longer registers login, logout, level, skill-update, skill-set, or
player-update hooks. Login and idle gameplay perform zero module queries and
zero reconciliation work. Remaining player events are the recognized-spell
learn hook (two asynchronous idempotent writes, no read or fan-out), the
recognized-spell forget hook needed for suppression provenance, and the
authenticated addon self-whisper request handler. Administrative ownership
changes update durable state but do not push changes to online characters.

Opening Collections -> Mounts sends one `SYNC` request with a monotonic client
request ID. The server validates the self-whisper and ID, loads ownership plus
character grant/suppression provenance with one batched `UNION ALL` read,
imports independently known spells in batches, evaluates the startup-loaded
catalog in memory using `GetPureSkillValue` for professions, grants/revokes only
this character, and sends a request-tagged snapshot. The client ignores older
responses and refreshes rows and the selected preview on the matching `END`.
Closing does nothing. Reopening issues a new request. There is no polling.

## Query and timing accounting

| Path | Before | After |
|---|---:|---:|
| Character/playerbot login | 1 batched read minimum, plus writes and possible fan-out | 0 reads, 0 writes |
| Idle, level, riding, profession change | threshold-dependent reconciliation | 0 reconciliation queries |
| Independent recognized learn | 1 ownership read, writes, fan-out/snapshots | 0 reads, 2 asynchronous writes |
| Steady-state Collections open | ownership snapshot read only | 1 batched ownership+provenance read, 0 writes |
| Collections open with N new grants | not the reconciliation point | 1 read, N synchronous pre-learn provenance writes, N asynchronous active updates |

The request log reports `db_reads`, `sync_writes`, `grants`, `revokes`,
`imports`, `query_us`, and total `world_thread_us`. A live database/worldserver
was not available for trustworthy latency values; PTR operators should collect
these fields under production database and bot load. The automated 1,000-login
contract establishes that no login callback exists, so catalog growth cannot
add module work to that path.

## Preview correction

The catalog already contains the authoritative creature entry from the mounted
aura's `MiscValue`; no hand-maintained mapping or generator change is needed.
`PlayerModel:SetCreature` resolves that entry through the client creature-query
cache. Learned or previously encountered mounts had cache entries, while
uncollected mounts often did not, causing the frame to retain an old or
incorrect model.

During the explicit Collections request the server now sends the normal
authoritative creature-query response for every unique catalog preview entry.
The client clears the model before every selection and reselects the current
mount after the tagged snapshot completes. It neither learns nor casts for
preview. Missing/invalid creature metadata leaves the model cleared and shows
`Preview unavailable`. Faction variants keep their own catalog creature entry.

## Verification and PTR checklist

Automated checks cover hook removal, the single batched read, no acquisition
read/fan-out, request sequencing, trained profession values, provenance-aware
decisions, no polling, preview cache priming, stale-model clearing, catalog
determinism, and deterministic EPF output. Full compilation and visual behavior
still require the target environment.

1. Enable query logging for `native_mount_*`; log in 1,000 playerbots and verify
   no module queries or snapshots occur from login or idle activity.
2. Open Mounts once and confirm one `SYNC`, one batched read, immediate row and
   selected-model refresh, and the logged query/world-thread timings.
3. Learn a mount on one character; verify ownership but no immediate changes on
   another. Open Mounts on each and verify eligibility, suppression,
   administrative revoke, and independent-spell protection.
4. Change level, riding, and permanent/temporary profession values. Verify no
   immediate reconciliation; reopen Mounts and confirm only permanent trained
   skill affects eligibility.
5. With an empty creature cache, select learned and uncollected Brown Ram,
   White Ram, Brown Elekk, and faction-equivalent pairs. Verify correct model
   and skin, invalid-preview clearing, and no learned-spell side effect.
6. Recheck dragging, persisted action buttons, summoning, cooldowns,
   combat/location restrictions, and Random Mount after opening the page.

No PTR or in-game visual validation is claimed by this repository change.
