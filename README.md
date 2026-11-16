# mod-native-mounts

`mod-native-mounts` adds an account-wide native mount collection to
[AzerothCore](https://www.azerothcore.org/) for World of Warcraft 3.3.5a
(client build 12340). It contributes a **Mounts** page to the shared Collections
interface, reconciles collected mounts into each eligible character's native
spellbook, and keeps normal mount spells usable from standard action bars.

The module owns its page and collection data independently. The Collections
window, client patch composition, and managed Random Mount spell are supplied
through [`mod-content-manager`](https://github.com/Hisha/mod-content-manager).

## Features

- Account-wide ownership stored independently from any one character
- Cross-character sharing with character-specific faction, race, class, level,
  riding, and profession eligibility
- Automatic catalog generation from effective World of Warcraft and AzerothCore data
- Scrollable browsing of collected and uncollected mounts
- **All**, **Collected**, and **Uncollected** filter states
- Creature-model previews, including uncollected mounts
- Native spellbook and action-bar integration for usable mounts
- A managed **Random Mount** ability that works after Collections is closed
- An independent Mounts page in the existing Content Manager Collections window

Collection ownership and character usability are deliberately separate. An
account can own a mount while the current character is unable to learn or use its
variant. The page displays that entry as **Collected - restricted on this
character** rather than treating it as uncollected.

## How account-wide collections work

Suppose Hipally learns a cataloged mount through normal gameplay. The learn hook
records the mount's logical collection key for Hipally's account and records that
Hipally's spell was learned independently of this module.

When Bullsha later opens **Collections -> Mounts**, the client requests a fresh
account snapshot. The server imports any independently known catalog mounts,
loads account ownership and Bullsha's grant history, and evaluates every
account-owned mount against Bullsha's character. Eligible native spell variants
are learned into Bullsha's spellbook. A faction, race, class, profession, level,
or riding restriction can therefore leave a mount collected for the account but
unavailable to Bullsha.

Logical collection entries can contain faction-specific spell variants. Owning
the entry means owning the collection item; reconciliation selects only a variant
that the current character is allowed to use.

The module distinguishes two sources of character spells:

- **Independently learned spells** come from gameplay, another system, or a
  pre-existing spellbook. They establish account ownership and are marked as
  independent. This module does not revoke them.
- **Module-granted spells** are learned during Collections reconciliation. Their
  provenance is committed before the spell is granted, allowing the module to
  revoke only spells that it introduced when ownership or eligibility changes.

If an independently known spell predates installation, open Mounts on that
character once to import it into account ownership. If a module-granted spell is
removed externally, a suppression record prevents an immediate re-grant loop.

## On-demand synchronization

Full reconciliation occurs only when a player opens **Collections -> Mounts**.
Each opening sends a versioned synchronization request; the server performs one
batched ownership/provenance read, reconciles that character, primes mount preview
data, and returns a tagged ownership snapshot. Closing the page performs no work,
and leaving it open does not poll.

In particular:

- Character login does not run a full mount reconciliation.
- Idle characters are not scanned on a timer.
- Level, riding, and profession changes do not initiate background scans.
- Playerbots do not reconcile collections merely by logging in.
- Learning a recognized mount records durable ownership, but does not push the
  change to other online characters.
- A newly shared or newly eligible mount may not become usable until that
  character opens the Mounts page.

This design keeps catalog-wide database and spellbook work off login and player
update paths. See [On-demand Collections reconciliation](docs/EVENT_DRIVEN_REFACTOR.md)
for the event map and query accounting.

## Random Mount

The `content/mod-native-mounts.epf` package declares Random Mount as a managed
spell. Content Manager allocates and applies its server/client identity. After
the corresponding build is active and the worldserver has restarted, opening
**Collections -> Mounts** teaches the ability to the character and displays it
at the bottom of the Mounts page.

Drag **Random Mount** from that button to any normal action-bar slot. Once learned,
the action works with Collections closed and persists as an ordinary spell action.

On each cast, the server:

1. considers catalog mount spells currently known by the character;
2. applies the catalog's static eligibility rules;
3. excludes administratively revoked, eligibility-revoked, and externally
   suppressed grants;
4. asks the native spell system whether each candidate can be cast in the current
   situation;
5. chooses one candidate per logical collection entry, then selects randomly; and
6. casts the selected public mount spell normally, without triggered flags that
   bypass gameplay checks.

The selected mount therefore retains AzerothCore's normal combat, movement,
location, water, flight, and other spell restrictions. If the character knows no
eligible catalog mount, or none is currently castable, the cast fails with the
corresponding native spell error. Random Mount does not automatically retry a
different mount after the final selected spell begins casting.

Random Mount is unavailable if its Content Manager resource is not in a verified
ACTIVE/APPLIED build, its server `SpellInfo` has not been loaded by a worldserver
restart, or the module has not successfully synchronized the character.

## Mount catalog

The catalog is generated offline rather than discovered during gameplay. The
generator reads:

- the effective, composed build-12340 `Spell.dbc`;
- an effective `item_template` SQL export; and
- `player_factionchange_spells` data.

Ordinary entries must have a durable mount-teaching item, resolve through their
spell trigger chain to `SPELL_AURA_MOUNTED`, and exist in the effective DBC.
Reviewed trainer-only class mounts are included through an explicit overlay.
This excludes test, NPC, temporary, quest, rental, vehicle, and internal
implementation spells that are not durable player collectibles.

The generator derives names, icons, minimum level, riding requirements, source
item restrictions, race/class masks, trained profession requirements, and preview
creature entries from those sources. Faction-change equivalents are grouped under
one logical collection key while retaining their distinct native spells and model
previews.

The same generation produces the compiled server catalog, review JSON, and client
Lua catalog. The current checked-in size is a generated baseline, not a fixed
limit.

Custom mounts are compatible when their effective composed data follows the same
durable convention: a public mount spell with a resolvable mounted-aura chain and
a normal durable teaching item carrying its restrictions. Generate after Content
Manager composition so custom spell, icon, creature, item, and faction data are in
the inputs. Trainer-only custom mounts without durable teaching metadata are not
automatically admitted.

See [Complete generated mount catalog report](docs/COMPLETE_CATALOG_REPORT.md) for
the discovery rules, generated artifacts, and review statistics.

## Installation and requirements

### Requirements

- World of Warcraft 3.3.5a client build 12340
- An AzerothCore source tree compatible with this module's hooks
- [`mod-content-manager`](https://github.com/Hisha/mod-content-manager), required
  for the Collections host, EPF composition, managed Random Mount spell, and
  client patch build
- A C++17 compiler and the normal AzerothCore build/database prerequisites

This repository was compiled against commit
`32fd3472aa74961c91cb9bc942ee3e0a44b36779` of the
[`mod-playerbots/azerothcore-wotlk`](https://github.com/mod-playerbots/azerothcore-wotlk)
**Playerbot** branch. Use that branch for a Playerbots deployment. The
`mod-playerbots` gameplay module itself is not required by `mod-native-mounts`;
the on-demand design simply ensures bot logins do not trigger collection work.
Compatibility with other AzerothCore revisions should be verified by building
and testing against that revision.

[`Portalkeeper`](https://github.com/Hisha/Portalkeeper) is optional deployment
tooling. It can distribute and verify the cumulative MPQ for players, but neither
the server module nor Content Manager requires Portalkeeper to operate.

### Server modules

For a Playerbots-based source tree, the verified repository layout can be created
with:

```bash
git clone https://github.com/mod-playerbots/azerothcore-wotlk.git --branch Playerbot
cd azerothcore-wotlk/modules
git clone https://github.com/Hisha/mod-content-manager.git
git clone https://github.com/Hisha/mod-native-mounts.git
```

Configure, build, and install AzerothCore using its normal platform instructions.
Both modules are discovered through the standard `modules/` build process. On
startup, AzerothCore's module database updater applies this repository's
`data/sql/db-characters` schema to the characters database; do not apply the base
and update files as two separate ad hoc migrations.

Install the generated module configuration files with the rest of the server
configuration, configure Content Manager's module, work, output, baseline DBC,
and publish directories, and make those paths writable by the worldserver where
required. Content Manager must be able to discover
`modules/mod-native-mounts/content/mod-native-mounts.epf`.

### Build and activate the client content

From the worldserver console or an administrator account:

```text
.content scan
.content install mod-native-mounts
.content build
.content build list
.content activate <build-number>
```

Activation applies the managed server spell before publishing the cumulative
client MPQ. Install that MPQ in the build-12340 client, then restart both the
client and worldserver. The restart is required for AzerothCore to reload the
managed `spell_dbc` row and build Random Mount's `SpellInfo`.

Finally, set `NativeMounts.Poc.Enable = 1`, restart worldserver, log in, and open
**Collections -> Mounts**. Confirm the startup log reports a loaded catalog and a
resolved Random Mount resource before enabling the module for players.

For automated client distribution, publish the active MPQ and configure a stable
Portalkeeper `[Patch.realm-content]` entry using `InstallMode=WowPatch`. Keep the
same realm identity and patch key when updating its URL and SHA-256 so
Portalkeeper reuses its allocated patch slot. This distribution step is optional;
the client patch itself is required.

## Configuration

The module currently exposes two worldserver options:

| Option | Default | Behavior |
| --- | ---: | --- |
| `NativeMounts.Poc.Enable` | `0` | Enables ownership tracking, Collections synchronization, grants, and Random Mount after the database and client/server content are deployed. |
| `NativeMounts.Poc.RevokeIneligibleModuleGrants` | `1` | Removes a now-ineligible or no-longer-owned spell only when the active provenance row shows that this module introduced it. Set to `0` for grant-only diagnostics. |

The `Poc` segment remains part of the implemented configuration keys and must not
be omitted. There is no reconciliation interval, login-sync, catalog-size, or
Random Mount ID setting. Random Mount's numeric spell ID is allocated and resolved
by Content Manager and should not be hardcoded.

## Database and persistence

The characters database contains these module tables:

- `native_mount_account_ownership` stores a logical mount key per account,
  including the first observed character/spell, acquisition source, and owned or
  administratively revoked state. It intentionally has no character foreign key,
  so deleting the character that first acquired a mount does not delete account
  ownership.
- `native_mount_character_grant` records per-character spell provenance and its
  pending, active, eligibility-revoked, independent, or externally suppressed
  state.
- `native_mount_favorite` reserves account-scoped favorite records in the schema;
  the current Collections UI and Random Mount selection do not read or write it.

Grant provenance is the safety boundary for revocation. Before learning a spell,
the module records that it is introducing the grant. It removes a spell only when
that record still identifies an active module grant and revocation is enabled.
Independently learned spells are recorded with non-module provenance and preserved.

An external unlearn that is not an eligibility-driven module removal creates a
suppression tombstone. Reconciliation will not repeatedly restore that spell until
an operator deliberately clears or changes the relevant state. Administrative
ownership commands create or update durable ownership tombstones:

```text
.native-mounts ownership grant <account-id> <mount-key>
.native-mounts ownership revoke <account-id> <mount-key>
```

These commands update persistence only; affected online characters reconcile the
next time they open Mounts.

Account-wide means accounts sharing this characters database. Separate character
databases are not synchronized merely because their authentication account IDs
match.

## Troubleshooting

### Collections does not populate

- Confirm `NativeMounts.Poc.Enable = 1` is loaded.
- Check the worldserver log for missing `native_mount_account_ownership` or
  `native_mount_character_grant` tables.
- Run `.content scan` and `.content build list`; the package must be installed
  and present in the active cumulative build.
- Verify the client has that active MPQ and was restarted after installation.
- Confirm Content Manager generated the Collections host and loaded
  `NativeMountsFrame.xml` after `ContentManagerCollections.lua`.

### A collected mount is restricted

This is normally expected. Ownership is account-wide; usability is not. Check the
character's faction/race, class, level, riding skill, and—where applicable—pure
trained profession rank. Temporary profession bonuses do not satisfy the catalog
gate. Reopen Mounts after correcting eligibility.

### A newly acquired mount does not appear on another character

Learning records ownership but does not fan out reconciliation. Open
**Collections -> Mounts** on the acquiring character if the spell existed before
the module, then open it on the other character. There is no login or periodic
background synchronization.

### Random Mount is missing or does not work

- Open Mounts once on the character; that synchronization teaches the ability.
- Confirm the package is ACTIVE/APPLIED in Content Manager and restart worldserver
  after activation so the managed spell is loaded.
- Look for `Random Mount managed spell is unavailable` in the worldserver log.
- Confirm the character has at least one eligible, currently castable catalog
  mount. Native combat, movement, map, water, riding, and flight restrictions
  still apply.
- If the Collections button is disabled, leave combat, reopen Mounts, and allow
  spellbook events to refresh the secure button before dragging it.

### Mount previews do not render or show the wrong model

Open or reopen Mounts to request preview creature-cache priming, then reselect the
entry. Confirm the server world data contains the catalog's creature template and
that the client patch contains compatible DBC/model data. Entries with missing or
invalid preview metadata intentionally show **Preview unavailable**.

### The client patch is missing or outdated

Compare the installed patch with the filename and SHA-256 reported by the active
Content Manager build. Reinstall the cumulative patch and restart the client. When
using Portalkeeper, update the existing stable patch key rather than creating a
second key that can leave multiple cumulative MPQs installed.

### Server and client catalogs do not match

Run the automated checks below. Regenerate all catalog outputs from the same
effective composed DBC and SQL inputs used by the server, rebuild the EPF, create
and activate a new cumulative Content Manager build, then update the client patch.
Do not hand-edit `NativeMountCatalog.inc` or `NativeMountCatalog.lua`.

## Development and testing

Run the repository verification suite from its root:

```bash
python3 tests/run_checks.py
```

It runs the Python generator/contract tests, builds and runs the standalone C++17
eligibility tests with warnings as errors, and verifies that the checked-in EPF is
deterministic and current. A full worldserver build remains the required integration
check for the target AzerothCore revision.

To regenerate the catalog, supply exports from the same effective server/client
content set:

```bash
python3 tools/generate_mount_catalog.py \
  --spell-dbc /path/to/effective/Spell.dbc \
  --item-template /path/to/effective/item_template.sql \
  --faction-spells /path/to/effective/player_factionchange_spells.sql
python3 content/build_epf.py
python3 tests/run_checks.py
```

Use `--check` with the generator to validate those inputs against the checked-in
outputs without rewriting them. Catalog generation updates:

- `generated/mount_catalog.json`
- `generated/NativeMountCatalog.inc`
- `content/client/Interface/FrameXML/NativeMountCatalog.lua`

`python3 content/build_epf.py` then rebuilds
`content/mod-native-mounts.epf`. When changing mount content delivered through
EPF composition, compose the effective DBC/server exports first, regenerate the
catalog, rebuild the EPF, run the checks, and produce a new cumulative Content
Manager build.

The deeper design and verification material remains available in:

- [Account-wide architecture](docs/ACCOUNT_WIDE_ARCHITECTURE.md)
- [On-demand reconciliation](docs/EVENT_DRIVEN_REFACTOR.md)
- [Catalog generation report](docs/COMPLETE_CATALOG_REPORT.md)
- [Technical investigation](docs/TECHNICAL_INVESTIGATION.md)

## License and credits

`mod-native-mounts` is released under the [MIT License](LICENSE). Copyright
&copy; 2026 Kevin Smith.

The module is built for [AzerothCore](https://www.azerothcore.org/) and uses
World of Warcraft 3.3.5a client/server data conventions. Collections composition
and managed client/server resources are provided by
[`mod-content-manager`](https://github.com/Hisha/mod-content-manager). Optional
client patch distribution is supported by
[`Portalkeeper`](https://github.com/Hisha/Portalkeeper).
