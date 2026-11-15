# mod-native-mounts technical investigation

Date: 2026-11-14

Target: AzerothCore / WoW 3.3.5a build 12340

Scope: source investigation and architectural starting point

> **2026-11-15 decision update:** [ACCOUNT_WIDE_ARCHITECTURE.md](ACCOUNT_WIDE_ARCHITECTURE.md)
> is the authoritative architecture for account-wide functional mounts, native
> action-bar support, provenance-aware spell grants, favorites, and the isolated
> proof of concept. The source findings in this document remain applicable, but
> its earlier preference for journal-only casting is superseded because it cannot
> satisfy the confirmed normal-action-bar requirement.

## Evidence and confidence

This report uses the following labels:

- **Verified**: directly established by the checked-out source, SQL schema/data, a build-12340 DBC, or an existing 3.3.5a client implementation.
- **Inferred**: strongly supported by those sources but still needs an in-client PTR check.
- **Proposed**: recommended module design, not current behavior.

Repositories inspected at these commits:

| Repository | Commit |
|---|---|
| `azerothcore-wotlk` | `32fd3472aa74961c91cb9bc942ee3e0a44b36779` |
| `mod-native-transmog` | `408865d559617b9ebb9295791f9ef82007fdb5eb` |
| `mod-content-manager` | `5cd44c08375ed16ff3e787ee22249d5cf0d939d1` |
| `Portalkeeper` | `09703e8c8ad4680bafee865312c0615d3bb51c89` |
| local WotLK DBM checkout | `19dc9ec5b46fd2ad336591f1273ca2daf9d6d03e` |

The supplied layout reference is `design/Collection_Mount_Interface.jpg`. It depicts a search/filter header, three-column scrollable mount list, collected/favorite states, right-hand details and model, summon/random controls, count, and Collections tabs. It is treated as a functional reference only; no pictured retail asset should be copied.

## 1. Executive summary

**Feasibility: high for a polished mount list, ownership states, search/filtering, favorites, random favorites, and creature-only 3-D preview; conditional for account-wide ownership; not feasible for an arbitrary player-riding-selected-mount preview with the exposed 3.3.5a model API.**

The most important architectural findings are:

1. **Collections is already shared infrastructure.** `mod-content-manager` generates the micro-menu button, outer Collections host, page registry, page tabs, title, dimensions, and original artwork. `mod-native-transmog` merely registers an `appearances` page in its EPF manifest. A Mounts page can register independently with `id: "mounts"`; transmog needs no source change or dependency. Evidence: `mod-content-manager/src/ContentFrameXml.cpp:421-522`, `mod-content-manager/docs/NATIVE_COLLECTIONS_MENU.md`, and `mod-native-transmog/content/manifest.json:19-43`.
2. **WotLK has no mount collection object.** A collected mount is normally a learned spell in per-character `character_spell`. The mount spell applies `SPELL_AURA_MOUNTED`; that effect's `MiscValue` is a creature entry, which resolves through `creature_template_model` to a `CreatureDisplayInfo` ID and then a client model. Evidence: AzerothCore `Player.cpp:3160-3699`, `PlayerStorage.cpp:6640-6664,7894-7934`, `SpellAuraEffects.cpp:3389-3445`, and `character_spell.sql`.
3. **The raw mount-aura test is insufficient for a catalog.** The inspected build-12340 `Spell.dbc` has 49,839 rows, 234 fields per row, and embeds all three spell effects. It contains 396 spells with `SPELL_AURA_MOUNTED`, referencing 324 unique creature entries, including legitimate player mounts, obsolete duplicates, test spells, temporary event/rental forms, quest/NPC mounts, and transformations. There is no separate `SpellEffect.dbc` in this client/core layout. A complete journal therefore needs automated candidate discovery plus a reviewed allow/deny and canonical-identity layer.
4. **Creature-only preview is native.** Build-12340 exposes `PlayerModel:SetCreature`, `SetFacing`, `SetRotation`, `SetModelScale`, `SetPosition`, `SetSequence`, and camera selection. The local WotLK DBM uses these APIs for arbitrary creature previews (`DBM-GUI/DBM-GUI.lua:550-558,974-1017`). The preview identifier should be a creature entry, not a raw display ID. There is no verified build-12340 `SetDisplayInfo`, `SetPortraitZoom`, or `SetCamDistanceScale` API.
5. **Recommended ownership is account ownership plus character eligibility, stored independently from the spellbook.** Import existing `character_spell` rows into an account collection and record subsequent mount learning through the existing `OnPlayerLearnSpell` hook. Do not automatically teach every account-owned spell by default. The journal can ask the server to perform a non-triggered cast after module authorization; AzerothCore then retains its ordinary cast/location/combat/flying validation. This avoids polluting every character's spellbook and keeps “owned” distinct from “usable.” It does mean stock WotLK mount achievements remain character-spellbook achievements unless a deliberate compatibility policy changes them.
6. **No AzerothCore core patch is currently unavoidable.** Player learn/forget/login/delete hooks, addon-whisper routing, spell APIs, DBC stores, and ordinary cast validation are available to a module. A semantic EPF registration mechanism for custom mounts would require an additive `mod-content-manager` feature, not an AzerothCore core change.

## 2. Existing Collections architecture

### 2.1 Micro-menu button and host ownership

**Verified.** `ContentFrameXml::ComposeCollectionsLua` generates `ContentManagerCollectionsFrame` and `ContentManagerCollectionsMicroButton`. The button:

- uses stock Character micro-button up/down chrome;
- overlays an original Content Manager chest icon;
- toggles the generated Collections host;
- anchors after `LFDMicroButton` and moves `MainMenuMicroButton` after itself;
- re-applies that layout from `UpdateMicroButtons` and `MoveMicroButtons` hooks.

Source: `mod-content-manager/src/ContentFrameXml.cpp:421-522`. The emblem and micro-button textures are deterministic original TGAs generated by `ComposeCollectionsEmblem` and `ComposeCollectionsMicroButtonIcon`, not retail art (`ContentFrameXml.cpp:525-687`; `docs/NATIVE_COLLECTIONS_MENU.md:91-103`).

The host is a `DIALOG`-strata movable frame with stock gold dialog backdrop/header, close button, emblem, `UISpecialFrames` escape handling, contributor page routing, and bottom `CharacterFrameTabButtonTemplate` navigation. Page dimensions and title are selected from the active page. Source: `ContentFrameXml.cpp:454-501`.

### 2.2 Transmog's contribution

**Verified.** Transmog's EPF manifest contributes one page:

```json
{
  "id": "appearances",
  "name": "Appearances",
  "order": 100,
  "width": 704,
  "height": 616,
  "frame": "NativeTransmogCollectionsFrame",
  "loadTarget": "Interface/FrameXML/NativeTransmogFrame.xml",
  "onShow": "NativeTransmog_OpenCollections",
  "onHide": "NativeTransmog_CloseCollections"
}
```

Source: `mod-native-transmog/content/manifest.json:19-43`. Its page is initially hidden and parented to `ContentManagerCollectionsFrame` (`NativeTransmogFrame.xml:376-382`). The page itself has internal **Items** and **Sets** tabs (`NativeTransmogFrame.xml:402-416`; switching logic at `NativeTransmogFrame.lua:636-645`). Those are Appearances-local subviews, not the top-level Collections registry.

### 2.3 Multiple independent top-level tabs

**Verified.** Schema 3 accepts an array of `nativeCollectionPages`. Page IDs must be unique across the cumulative build; pages sort by `order`, case-folded name, then ID. Each contributor owns its page frame and optional callbacks, while Content Manager owns the host and top-level tabs. Validation requires the page `loadTarget` to be package-owned and listed in `clientFrameXml.loadEntries`. Sources:

- `mod-content-manager/src/ContentPackage.cpp:2019-2104`
- `mod-content-manager/src/ContentBuildService.cpp:226-340`
- `mod-content-manager/src/ContentFrameXml.cpp:421-522`
- `mod-content-manager/docs/NATIVE_COLLECTIONS_MENU.md`

Therefore other modules already can register collection pages independently. `mod-native-mounts` should contribute `id: "mounts"` with its own frame and callbacks. No registration API needs to be added to transmog.

### 2.4 EPF packaging and publication

**Verified.** Transmog packages its Lua/XML as deterministic ZIP/EPF content and pins the stock build-12340 `FrameXML.toc` SHA-256. Its `clientFrameXml.loadEntries` asks Content Manager to place `NativeTransmogFrame.xml` after the generated `ContentManagerCollections.lua`. Sources: `mod-native-transmog/content/build_epf.py`, `content/manifest.json:1-28`.

Content Manager composes all installed contributors into one cumulative build, rejects raw conflicts with its generated Collections files, inserts load entries into the verified stock TOC, stages the generated controller and two textures, records hashes, and publishes a versioned MPQ (`ContentBuildService.cpp:220-340,1215-1294`).

Portalkeeper distributes that cumulative MPQ as a realm patch. `InstallMode=WowPatch` allocates and retains one `patch-4.MPQ` through `patch-9.MPQ` slot per stable realm identity and patch key, verifies SHA-256, stages replacement safely, and reuses the same slot when URL/hash changes. Required invalid content blocks launch. Source: `Portalkeeper/README.md:112-172`, `src/Portalkeeper/Services/PatchService.cs`, and `WowPatchAllocationStore.cs`.

Operational requirement: continue publishing the cumulative build under the same realm identity and `[Patch.realm-content]` key while changing URL and SHA-256. A new key can leave two cumulative MPQs installed; `mod-content-manager/docs/NATIVE_COLLECTIONS_MENU.md:55-68` explicitly warns about this.

### 2.5 Existing client/server transport

**Verified.** Native transmog uses addon chat, not a custom opcode:

- client prefix `NTRANSMOG`, protocol version 5 (`NativeTransmogProtocol.h`);
- client calls `RegisterAddonMessagePrefix`, sends tab-delimited self-whispers with `SendAddonMessage`, and accepts only matching `CHAT_MSG_ADDON` self-whispers (`NativeTransmogFrame.lua:270,1211-1215,1456-1465`);
- server receives `LANG_ADDON` self-whispers through `PlayerScript::OnPlayerCanUseChat`, consumes requests, and allows only its known server response vocabulary back to FrameXML (`NativeTransmogModule.cpp:382-400`);
- server responses use `Player::Whisper(..., LANG_ADDON, player)` (`NativeTransmogWardrobe.cpp:227-228`);
- requests are versioned, bounded, paged, rate-limited, and server-resolved; mutation requests use server snapshots/nonces rather than trusting client item identity.

Mounts should copy this transport pattern under a distinct prefix such as `NMOUNTS`, not share transmog protocol/state. A Collections page does not require an NPC authorization token for read-only catalog access. Summoning needs ownership and eligibility validation, not Warpweaver authorization.

### 2.6 Ownership recommendation for the Collections shell

**Recommendation.** Keep the present split:

- `mod-content-manager`: shared Collections shell, button, page registry, host, tabs, generated artwork;
- `mod-native-transmog`: Appearances page and transmog-only subviews;
- `mod-native-mounts`: Mounts page and mount-only state/protocol.

Do not move the host back into transmog and do not make mounts depend on transmog. A new shared collection-interface module would duplicate infrastructure that Content Manager already provides. A small shared widget library should be considered only later if at least two pages need identical search/list/model widgets; it is not required for the first mount journal.

## 3. WotLK mount-system findings

### 3.1 Representation and visual resolution

**Verified.** A normal mount spell has an effect with `ApplyAuraName == SPELL_AURA_MOUNTED` (numeric aura 78). AzerothCore exposes only three effects per build-12340 spell (`MAX_SPELL_EFFECTS = 3`) and reads them from the monolithic `SpellEntry` structure. `SpellEntry` includes effect type, aura, misc values, trigger spell, icon, localized name, levels, area group, and other spell fields. Source: `azerothcore-wotlk/src/server/shared/DataStores/DBCStructure.h:1673-1774` and `DBCfmt.h:112`.

When the mounted aura is applied, `AuraEffect::HandleAuraMounted`:

1. reads the aura effect `MiscValue` as a **creature entry**;
2. loads its `CreatureTemplate`;
3. chooses a `CreatureDisplayID`, including random-gender handling;
4. obtains any vehicle ID;
5. calls `Unit::Mount(displayId, vehicleId, creatureEntry)`.

Source: `SpellAuraEffects.cpp:3389-3445`. The world table `creature_template_model(CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability, VerifiedBuild)` provides the creature-to-display mapping (`data/sql/base/db_world/creature_template_model.sql:23-37`). Client `CreatureDisplayInfo.dbc` maps display ID to `CreatureModelData` ID and scale; `CreatureModelData.dbc` contains the model path and geometry/collision data, though AzerothCore's compact runtime structures omit the path field (`DBCStructure.h:747-825`).

Consequences:

- spell ID is the gameplay/ownership identity in stock WotLK;
- creature entry is the best `SetCreature` preview handle;
- display ID is the actual mounted unit visual selected by the server;
- model-data ID/path is a lower-level asset identity, not a mount identity.

### 3.2 Learning and storage

**Verified.** Learning items normally cast a wrapper spell with `SPELL_EFFECT_LEARN_SPELL` (effect 36); `Spell::EffectLearnSpell` resolves `EffectTriggerSpell` and calls `Player::learnSpell` (`SpellEffects.cpp:2692-2716`). Some mount items use special wrapper spells 483 or 55884, for which AzerothCore takes the taught spell from the effect value.

`Player::learnSpell` routes through `addSpell`, rejects an already-active spell, updates the in-memory `PlayerSpellMap`, sends the learn packet, invokes `OnPlayerLearnSpell`, handles ranks/dependencies, and updates achievement criteria (`Player.cpp:3160-3510`). Permanent spells save to:

```sql
character_spell(guid, spell, specMask)
PRIMARY KEY (guid, spell)
```

Source: `data/sql/base/db_characters/character_spell.sql`; load/save are `PlayerStorage.cpp:6640-6664,7894-7934`; the prepared load query is `CharacterDatabase.cpp:78`.

There is no stock account mount table and no account-wide mount subsystem in this AzerothCore checkout. Ownership is character spellbook state.

### 3.3 Removal

**Verified.** `Player::removeSpell` marks/deletes the `PlayerSpell`, removes owned/triggered auras, adjusts dependent skills/profession points where applicable, recalculates the special 310%-flyer flag, invokes `OnPlayerForgotSpell`, and sends the unlearn packet (`Player.cpp:3513-3699`). `_SaveSpells` deletes the durable `character_spell` row (`PlayerStorage.cpp:7894-7934`).

The module can observe learn and forget without a core patch: `PLAYERHOOK_ON_LEARN_SPELL` and `PLAYERHOOK_ON_FORGOT_SPELL` are exposed in `ScriptDefines/PlayerScript.h:68-69,327-330` and dispatched by `PlayerScript.cpp:165-172`.

### 3.4 Mount achievements

**Verified.** Learning a spell updates `ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL`, `LEARN_SKILL_LINE`, and `LEARN_SKILLLINE_SPELLS` for its `SkillLineAbility` rows (`Player.cpp:3406-3414`). For `LEARN_SKILLLINE_SPELLS`, `AchievementMgr` scans the character's `PlayerSpellMap`, counts each spell having the requested skill line once, and compares that count to the DBC criterion target (`AchievementMgr.cpp:1539-1561,1963-1965`). Mounts use `SKILL_MOUNTS = 777` (`SharedDefines.h:3238`).

Thus stock mount-count achievements are character-specific and spell-based. An independent account collection will not automatically increment them. Teaching account mounts would increment them because the spells become character spells, but that is a policy change, not a free side effect to accept accidentally.

### 3.5 Riding skill and adaptive mounts

**Verified.** Riding is skill 762 (`SKILL_RIDING`, `SharedDefines.h:3224`), normally 75/150/225/300. Acquisition items commonly enforce it using `item_template.RequiredSkill`, `RequiredSkillRank`, `RequiredLevel`, and sometimes `RequiredSpell`. `Player::CanUseItem` checks faction flags, allowable class/race, skill/rank, required spell, level, and holiday (`PlayerStorage.cpp:2350-2435`). Examples from `item_template.sql`:

- Sea Turtle item 46109: riding 75, level 80, teaches spell 64731.
- Mechano-hog 41508: riding 150 and Horde race mask, teaches 55531.
- Mekgineer's Chopper 44413: riding 150 and Alliance race mask, teaches 60424.
- Flying Machine Control 34060: Engineering 300 plus Expert Riding spell 34090, teaches 44153.
- Turbo-Charged control 34061: Engineering 375 plus Artisan Riding 34091, teaches 44151.
- Flying Carpet 44554 and Magnificent Flying Carpet 44558: Tailoring requirements plus riding spell requirements.

Some learned “public” mount spells are adaptive wrappers. `spell_gen_mount` selects a triggered 60/100/150/280/310-speed spell from the character's current riding skill, map, Cold Weather Flying, no-fly area, battlefield, and 310-flyer state (`scripts/Spells/spell_generic.cpp:4141-4246,6349-6357`). This covers the broom, Headless Horseman's Mount, Big Blizzard Bear, Winged Steed, and several late-Wrath mounts through `spell_script_names.sql`.

Implication: do not collapse a public wrapper spell and its internal speed-specific aura spells into separate collectible entries. The catalog must identify the learned/public spell and retain its triggered implementation spells only as mechanics.

### 3.6 Faction, class, and profession restrictions

**Verified.** Restrictions are distributed rather than represented by one “mount requirement” structure:

- source item faction flags and `AllowableRace`/`AllowableClass` are enforced by `Player::CanUseItem`;
- `SkillLineAbility.dbc` supplies `RaceMask`, `ClassMask`, `MinSkillLineRank`, and skill association (`DBCStructure.h:1633-1650`);
- `Player::IsSpellFitByClassAndRace` checks the skill-line race/class masks and `SkillRaceClassInfo` (`Player.cpp:12660-12687`);
- trainers provide required skill/rank, level, and prerequisite spell (`npc_trainer.sql:23-32`);
- spell area/condition data can add area, quest, aura, race, gender, and arbitrary server conditions (`spell_area.sql`; `Spell.cpp:6070-6105` condition check);
- class mounts include class-specific spell families/learn paths (for example paladin/warlock/DK mounts);
- profession mounts are primarily protected at item acquisition by profession skill and riding prerequisites; those requirements must be retained as journal usability metadata if the account collection can expose the mount to a different character.

Faction conversion is explicit in `player_factionchange_spells`. It maps many Alliance/Horde mount spells (for example Brown Horse 458 to Brown Wolf 6654, riding racial mounts, and class-mount variants). The conversion is loaded by `ObjectMgr::LoadFactionChangeSpells` and applied during faction change (`ObjectMgr.cpp:10977-11009`; `CharacterHandler.cpp:2465`). This is valuable evidence for grouping faction variants under one account collection key while selecting a team-appropriate usable spell.

**Important limitation.** Once an ordinary mount spell is learned, not every original source-item restriction is rechecked by the generic client-cast path. The handler first verifies that the character knows the active spell (`SpellHandler.cpp:386-520`), then `Spell::CheckCast` evaluates spell state, conditions, location, and aura-specific rules. Therefore an account-owned journal that summons an unlearned spell must explicitly enforce catalog eligibility before asking the spell system to cast it.

### 3.7 Summoning validation already present in core

**Verified.** Ordinary non-triggered spell preparation provides extensive reusable validation:

- combat and moving restrictions (`Spell.cpp:5980-6041`);
- vehicle/mounted state and “cast while mounted” attributes (`Spell.cpp:6043-6070,6225-6236`);
- world `conditions` (`Spell.cpp:6072-6105`);
- battleground/arena and `SpellInfo::CheckLocation` checks (`Spell.cpp:6190-6223`);
- flying mounts above water only;
- dungeon/instance `AllowMount` restrictions;
- disallowed shapeshift/transform states;
- no-fly areas and battlefields (`Spell.cpp:6951-7010`);
- spell-script behavior such as adaptive riding-speed selection.

The journal must not use `TRIGGERED_FULL_MASK`, `TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE`, or direct `Unit::Mount`. Those would skip or bypass important gameplay checks. The module should authorize ownership/eligibility and then start the public spell as a normal, non-triggered cast so AzerothCore remains authoritative.

## 4. Mount catalog discovery strategy

### 4.1 DBCs that actually exist in build 12340

**Verified.** AzerothCore loads these relevant build-12340 stores:

- `Spell.dbc`
- `SkillLine.dbc` / `SkillLineAbility.dbc` / `SkillRaceClassInfo.dbc`
- `CreatureDisplayInfo.dbc` / `CreatureDisplayInfoExtra.dbc`
- `CreatureModelData.dbc`
- `Item.dbc` / `ItemDisplayInfo.dbc`
- `SpellIcon.dbc`

Source: `DataStores/DBCStores.cpp:298-365` and `DBCStores.h:97-177`. It does **not** load or define a separate `SpellEffect.dbc`; build 12340 keeps the three effects inside each `Spell.dbc` row (`DBCStructure.h:1673-1774`). `Item.dbc` is only a compact client identity/display record (`ItemEntry` at `DBCStructure.h:1178-1188`); acquisition spell slots and restrictions come from the server `item_template`, not `Item.dbc`.

The inspected `mod-content-manager/reference/dbc/Spell.dbc` is a WDBC with:

- 49,839 records;
- 234 fields;
- 936-byte rows;
- 2,317,797-byte string block.

A direct scan found 396 spells with aura 78 and 324 unique nonzero creature-entry references. The results visibly contain false positives and duplicates such as `Mount (Test Anim)`, `[DNT] Test Mount`, `Video Mount`, `Rental Racing Ram`, quest mounts, repeated Reindeer/Headless Horseman variants, and speed-specific implementation spells. This is direct evidence that aura scanning alone cannot define the journal.

### 4.2 Recommended generation pipeline

**Proposed.** Generate a reviewed catalog artifact at development/build time, not dynamically on every world startup:

1. **Candidate discovery**
   - include spells associated with `SKILL_MOUNTS` (777) through `SkillLineAbility.dbc`;
   - require either a `SPELL_AURA_MOUNTED` effect or a reviewed wrapper that ultimately triggers mount auras;
   - record the mounted effect's creature entry, speed auras, icon, localized DBC name, spell level, area group, and script binding;
   - find teaching wrappers/items by scanning all `item_template.spellid_1..5` and those spells' `SPELL_EFFECT_LEARN_SPELL` targets;
   - find trainer, quest reward, faction-change, `spell_area`, `conditions`, and `spell_script_names` relationships.
2. **Mechanical classification**
   - public learned spell versus internal triggered/speed spell;
   - ground/flying/aquatic/adaptive/vehicle-like;
   - Alliance/Horde/neutral; class/profession/riding requirements;
   - preview creature entry and all possible display IDs.
3. **Automated rejection flags**
   - no `SKILL_MOUNTS` association;
   - obvious test/DNT/copy/video naming;
   - temporary area/quest/rental spell with no durable learn source;
   - NPC/vehicle/taxi-only relationship;
   - missing creature template/display/model chain;
   - internal spell taught by no durable source and referenced only by a wrapper.
4. **Curated overlay**
   - explicit include/exclude;
   - canonical logical `mount_key`;
   - faction/class variants under that key;
   - player-facing category, source text, description, expansion, sort order;
   - preview override and framing preset;
   - source-item override and usability policy.
5. **Validation output**
   - fail on a new unreviewed candidate, disappeared accepted spell, missing model chain, duplicate spell assignment, duplicate logical key, unresolved variant, or unknown requirement;
   - emit a deterministic C++/JSON catalog plus a human review report.

### 4.3 Identity and duplicate policy

Do not deduplicate solely by creature entry, display ID, model-data ID, icon, or name:

- multiple legitimate mounts can share a model with different skins;
- different creature entries can select the same display;
- obsolete and live spells can use the same creature entry;
- faction equivalents should usually be one account collection identity but two usable spell variants;
- wrapper and speed-specific spells represent one collectible;
- visually identical mounts can still be separate WotLK achievement-counting spells.

Recommended identity:

- stable string key, e.g. `wotlk:spell:32235` for a normal mount;
- curated shared key for faction equivalents, e.g. `wotlk:faction-pair:<stable-symbol>`;
- one-to-many variant rows containing public spell ID, team/class rules, source item, preview creature, and priority.

The collection counter should count logical catalog keys. A separate compatibility metric may show “known mount spells” if exact WotLK achievement parity is needed.

### 4.4 Custom EPF mounts

**Verified current limitation.** Content Manager semantically composes `Spell.dbc`, `Item.dbc`, currencies, costs, and selected other resources, but its inspected manifest has no native mount-catalog declaration and no semantic `CreatureDisplayInfo`/`CreatureModelData` allocation contract. Raw files can be packaged, but cumulative raw DBC ownership conflicts with semantic composition (`ContentBuildService.cpp:321-340`).

**Recommended extension.** Add an optional schema-3-compatible typed declaration to Content Manager, conceptually:

```json
"nativeMounts": [{
  "symbol": "obsidian_gryphon",
  "spell": "spell-symbol",
  "sourceItem": "item-symbol",
  "previewCreature": "creature-symbol",
  "category": "Flying",
  "sourceText": "...",
  "description": "...",
  "faction": "Both",
  "requirements": { "riding": 225 }
}]
```

Content Manager should resolve package-local symbols to allocated IDs and publish resolved server rows in its existing server bundle/ownership lifecycle. `mod-native-mounts` reads the installed resolved rows through a small optional `MountCatalogApiV1` or a Content Manager-owned world table. Removal/replacement must follow Content Manager package lifecycle so stale catalog entries disappear with the package.

Until that exists, custom modules can optionally register at world startup through a module-owned `MountCatalogApiV1`, but an EPF-only package cannot safely communicate allocated IDs to the server mount catalog. Do not require `mod-native-transmog` for this.

## 5. Client model-preview feasibility

### 5.1 Confirmed build-12340 surface

**Verified source-level capability.** A `PlayerModel` can display a creature by entry. The local WotLK DBM creates `PlayerModel`, calls `SetCreature(creatureid)`, and controls scale, facing, position, sequence, and sequence time (`DBM-GUI/DBM-GUI.lua:550-558,974-1017,1088-1163`). The build-12340 API inventory already documented by transmog confirms:

- `PlayerModel`: `SetUnit`, `SetCreature`, `RefreshUnit`, `SetRotation`;
- `Model`: `SetModel`, `ClearModel`, `SetPosition`, `SetFacing`, `SetModelScale`, `SetSequence`, `SetSequenceTime`, `AdvanceTime`, `SetCamera`, and lighting/fog controls;
- `DressUpModel`: `Undress`, `Dress`, `TryOn`.

Source: `mod-native-transmog/docs/EQUIPMENT_FOCUSED_3D_RENDERING_INVESTIGATION.md:26-89` and `PHASE2_RETAIL_PORTRAIT_INVESTIGATION.md:16-26`.

### 5.2 Feature matrix

| Feature | Finding |
|---|---|
| Display mount creature | **Verified feasible.** Use XML-owned `PlayerModel` and `SetCreature(previewCreatureEntry)`. |
| Rotate | **Verified feasible.** Buttons/drag/wheel can adjust `SetFacing` or `SetRotation`. Use one consistently after PTR testing. |
| Zoom | **Feasible with limitations.** Use curated `SetModelScale` and `SetPosition`; build 12340 has no verified `SetPortraitZoom` or `SetCamDistanceScale`. |
| Flying mount | **Inferred feasible as a creature M2.** It should render regardless of world flight permission, but animation/camera/framing vary and require live testing. |
| Unlearned mount | **Inferred feasible.** `SetCreature` has no learned-spell parameter and DBM previews arbitrary creature entries. Verify an uncached/unseen mount on PTR. |
| Player riding selected mount | **Not supported as a general journal preview.** `SetUnit("player")` and `SetCreature` replace one model source; no exposed API attaches an arbitrary mount display/aura to a model-only player or composites two M2s with the live attachment system. |
| Custom model | **Conditionally feasible.** The client must have the model, textures, display/model DBC rows, and a resolvable creature entry. `SetModel(path)` is a fallback but has weaker camera/framing semantics. PTR validation is mandatory. |

Practical alternative to a rider preview: show the mount alone in the right pane, using curated framing and an idle/fly sequence when reliable. The actual character riding it remains visible only after a real successful summon in the world. A decorative static silhouette of a rider would be misleading and should not be used.

### 5.3 Framing and rendering risks

Transmog's live experience shows that 3-D widgets are not reliably clipped inside scroll children, `SetUnit` initializes asynchronously, and scale/position/camera need per-use presets (`PHASE2_RETAIL_PORTRAIT_INVESTIGATION.md:33-56`; `EQUIPMENT_FOCUSED_3D_RENDERING_INVESTIGATION.md`). The mount journal should use exactly one fixed preview model outside the scrolling list. Do not create a 3-D model in every list row.

Store optional per-mount `camera`, `scale`, `x/y/z`, `facing`, and `sequence` overrides with category defaults. If `SetCreature` fails or a display chain is absent, show the spell icon and a “Preview unavailable” message rather than an empty black pane.

## 6. Account-wide ownership alternatives

### A. Character-specific collection

**Design.** Treat `Player::HasSpell(publicMountSpell)` / `character_spell` as ownership. Favorites use `(character_guid, mount_key)`.

Advantages:

- exact WotLK semantics and achievement compatibility;
- existing learn/remove and character deletion behavior naturally apply;
- no ownership reconciliation beyond loading the character spell map;
- native spellbook and journal agree.

Costs/behavior:

- mounts do not follow the account;
- each character must acquire/learn separately;
- deleting a character deletes its collection when AzerothCore finally deletes character rows;
- faction/class/profession behavior stays close to original acquisition paths.

This is the lowest-risk first milestone and useful as a diagnostic mode.

### B. Fully account-wide, unrestricted use

**Design.** Any account-owned logical mount can be summoned by every character, optionally teaching every variant spell.

Assessment: **not recommended.** It erases meaningful WotLK class, faction, profession, riding, and level restrictions. Teaching all spells inflates character achievements, pollutes spellbooks, complicates faction variants, and can create spells the client/core never expected that character to know. Casting without teaching still needs a new eligibility policy; if that policy is “always,” it intentionally bypasses WotLK restrictions.

It is implementable as a custom gameplay mode but should be an explicit non-default configuration, not called WotLK-compatible.

### C. Account-wide ownership, character-specific usability

**Design.** Account collection says “this account owns logical mount X.” A variant resolver decides whether the current character can use X now. The spellbook remains independent evidence and a compatibility surface.

Advantages:

- retail-inspired ownership without discarding WotLK restrictions;
- one durable account row survives character deletion;
- faction variants can resolve per current team;
- a low-level or wrong-profession character can see “collected, currently unusable” with a reason;
- existing character spells remain untouched.

Costs:

- requires a curated eligibility model because source-item restrictions are not universally rechecked after learning;
- stock achievements still count character spellbook mounts;
- acquisition/removal policy and cross-realm scope must be explicit.

**Recommendation: C.** Keep ownership independent and do **not** automatically teach account mounts by default. Use the journal protocol for server-authorized normal casting. Existing known spells remain available in the ordinary spellbook and count toward stock achievements. An optional future “materialize eligible account mounts into spellbook” mode would require a grant ledger so the module removes only spells it granted and never fights native/GM ownership; it should not be phase 1.

### 6.1 Comparison across required operational concerns

| Concern | A. Character-specific | B. Fully account-wide/unrestricted | C. Account-owned/character-eligible |
|---|---|---|---|
| Database | `character_spell` is authority; favorite table only | Account collection plus favorites; optional derived grants | Account collection plus favorites; spellbook remains independent |
| Existing spellbooks | Exact match | Either polluted by mass teaching or bypassed by journal casts | Preserved; existing known mounts continue normally |
| Riding/level | Native acquisition and spell behavior | Intentionally ignored unless extra rules are reintroduced | Catalog eligibility first, then ordinary cast validation |
| Faction | Native per-character spells/faction conversion | All mounts usable unless intentionally filtered | Resolve curated team variant; deny if none |
| Class/profession | Native acquisition path | Lost in unrestricted mode | Curated/source-item-equivalent eligibility |
| Achievements | Exact stock character counts | Mass teaching can inflate every character; independent ownership does not count | Existing character spells count; account-only ownership does not |
| Existing characters | No migration | Backfill distinct account keys from all characters | Same backfill, while retaining each character spellbook |
| Acquisition | `learnSpell` | Learning on any character unlocks account | Learning on any character unlocks account |
| Removal | `removeSpell` removes ownership | Policy problem: one forget should not erase all account use | Monotonic account ownership; explicit account revoke |
| Character deletion | Collection disappears with character | Account rows survive | Account rows survive |
| Account persistence | Not applicable | Character DB account rows; explicit account purge needed | Same |
| Cross-realm | Per character DB | Shared only if collection storage is shared | Same |
| Synchronization | Per-session spell state | Account cache invalidation plus possible mass grants | Account cache invalidation; no spell grant fan-out |
| Performance | Cheapest | Cheap ownership set, expensive grant/reconcile mode | Cheap ownership set and on-demand eligibility |

### 6.2 Acquisition and reconciliation

Recommended account rules:

- On login, insert account ownership for every catalog public spell already in the player's spell map.
- On `OnPlayerLearnSpell`, if the spell maps to a catalog variant, `INSERT IGNORE` its logical key for the authenticated account.
- Provide a one-time/offline backfill: join `characters(account)` to `character_spell`, map recognized spell IDs to logical keys, and insert distinct account rows.
- Treat ownership as monotonic by default. `OnPlayerForgotSpell` removes the character spell but does not revoke account ownership. Add an explicit GM revoke operation for corrections.
- For character-specific mode, forgetting the spell immediately removes effective ownership because the spellbook is authoritative.
- Never accept account ID, owner ID, spell ID, or collection state from the client. Derive account ID from `Player::GetSession()->GetAccountId()`.

The composite primary key prevents duplicates; `INSERT IGNORE` plus a post-write/cache update follows the proven transmog collection pattern (`NativeTransmogCollection.cpp:21-29,185-286`).

### 6.3 Deletion, persistence, realms, and synchronization

- **Character deletion:** account rows remain; favorites may remain account-wide. Character-specific favorite rows should be deleted with `PLAYERHOOK_ON_DELETE_FROM_DB`, which is available (`PlayerScript.h:153,556`; `Player.cpp:4413`).
- **Account deletion:** a character-DB table cannot foreign-key an auth-DB account. Account-ID reuse is a risk. Operations need an explicit account purge command/hook or a durable account incarnation identifier if the auth system can reuse IDs.
- **Cross-realm:** account-wide means account-wide inside the configured collection database. Separate character databases do not synchronize merely because auth account IDs match. Real global collections require a shared database/service and conflict/latency design; keep that out of the initial module.
- **Multiple online characters:** commit ownership synchronously before publishing it to caches, then notify other sessions on the same worldserver or invalidate per-account cache generation. Cross-worldserver invalidation requires DB polling/pub-sub and is out of scope unless the deployment actually runs concurrent worldservers on one character DB.
- **Performance:** load a compact set of mount keys once per active account, page public catalog data, and keep ownership/favorites indexed. The WotLK catalog is small enough for an in-memory immutable catalog.

## 7. Retail-inspired journal feature feasibility

| Feature | Recommended 3.3.5a implementation |
|---|---|
| Mounts tab | Independent `nativeCollectionPages` entry, not a transmog inner tab. |
| Search | Debounced edit box; server-side normalized substring search over name/source/category, bounded length. Client-side search is acceptable only if the full authoritative page snapshot is already present. |
| Collected/uncollected | Server-provided bit/state. Use desaturation/dark overlay and text; do not trust client ownership. |
| Icon/name | Public spell ID with `GetSpellInfo`/`GetSpellTexture` where available; server fallback name/icon metadata for custom/cache failure. |
| Counter | `owned logical keys / visible or total catalog keys`; label semantics explicitly. |
| Categories/filters | Curated Ground/Flying/Aquatic/Adaptive plus Collected/Uncollected/Favorites/Usable. Avoid inventing unsupported retail families. |
| Preview | Single `PlayerModel:SetCreature` pane with curated scale/position/facing. |
| Source | Curated metadata; relationships can seed it but cannot produce polished prose reliably. |
| Description | Original project-authored or factual curated text. Do not copy retail journal prose/assets. |
| Summon | Addon request by server snapshot entry; server resolves owner, variant, eligibility, then performs normal cast. |
| Favorite | Server-persisted account favorite; star uses existing client texture or original simple art. |
| Random favorite | Server chooses uniformly among account-owned, favorite, currently eligible variants; returns a useful error if none. |

The supplied mockup's three narrow list columns are possible, but a virtualized single or two-column list is safer at 704–940 px and easier to use at low UI scale. The right detail/model pane is appropriate. Use stock dialog/list/button/scroll assets and original textures only. Do not reproduce later-client Collections icons, backgrounds, or typography by extraction.

## 8. Recommended module architecture

### 8.1 Server components

1. `MountCatalog`
   - immutable standard catalog loaded from a generated, reviewed artifact;
   - optional resolved custom entries;
   - indexes by logical key, public spell, source item, and preview creature.
2. `MountCollectionService`
   - account/character ownership strategy;
   - login/learn reconciliation;
   - cache, counts, and administrative grant/revoke/backfill.
3. `MountEligibilityService`
   - chooses a faction/class variant;
   - validates source-item-equivalent faction/class/race/level/riding/profession rules;
   - returns a stable unusable reason;
   - does not duplicate world/combat/location checks handled by normal spell casting.
4. `MountFavoriteService`
   - account or configured character favorites;
   - random eligible selection.
5. `MountJournalProtocol`
   - separate addon prefix/version;
   - bounded search/filter/page requests;
   - server snapshots/nonces for selection and summon;
   - rate limits, strict response vocabulary, self-whisper only.
6. `MountPlayerScript`
   - login/logout, learn/forget, character deletion, addon chat hooks;
   - no core patch.
7. Commands/diagnostics
   - catalog audit, account/character collection inspection, backfill preview/run, explicit grant/revoke, preview-chain validation.

### 8.2 Client components

- `NativeMountsFrame.xml`: page frame parented to `ContentManagerCollectionsFrame`, fixed right preview, scrollable list, search/filter widgets.
- `NativeMountsFrame.lua`: view state, protocol assembly/parsing, row virtualization, icon/name cache handling, model framing, favorites, summon UI.
- Optional original TGA/BLP artwork only where stock assets are insufficient.
- EPF manifest with one `nativeCollectionPages` declaration and load entry after `ContentManagerCollections.lua`.

Do not modify `NativeTransmogFrame.xml/.lua`. The host will hide/show each page and call its callbacks.

### 8.3 Configuration

Suggested options:

```ini
NativeMounts.Enable = 1
NativeMounts.Collection.Scope = Account       # Account | Character
NativeMounts.Account.RequireCharacterEligibility = 1
NativeMounts.Account.AutoTeachEligible = 0
NativeMounts.Collection.ImportExisting = 1
NativeMounts.Collection.RevokeOnForget = 0
NativeMounts.Favorites.Scope = Account        # Account | Character
NativeMounts.Journal.PageSize = 60
NativeMounts.Journal.RequestCooldownMs = 200
NativeMounts.Journal.SnapshotLifetimeMs = 30000
NativeMounts.CustomCatalog.Enable = 1
```

Reject the incompatible combination `Scope=Account`, unrestricted use, and “WotLK restrictions preserved” in documentation/config diagnostics.

## 9. Proposed database schema

Names are illustrative; migrations should follow AzerothCore module conventions.

### 9.1 Character database

```sql
CREATE TABLE native_mount_collection (
  account_id          INT UNSIGNED NOT NULL,
  mount_key           VARCHAR(96) CHARACTER SET ascii NOT NULL,
  first_character     INT UNSIGNED NOT NULL DEFAULT 0,
  first_spell_id      INT UNSIGNED NOT NULL DEFAULT 0,
  acquisition_source  TINYINT UNSIGNED NOT NULL,
  acquired_at         TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (account_id, mount_key),
  KEY idx_mount_collection_key (mount_key),
  KEY idx_mount_collection_character (first_character)
);

CREATE TABLE native_mount_favorite (
  owner_type  TINYINT UNSIGNED NOT NULL, -- 1 account, 2 character
  owner_id    INT UNSIGNED NOT NULL,
  mount_key   VARCHAR(96) CHARACTER SET ascii NOT NULL,
  created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (owner_type, owner_id, mount_key),
  KEY idx_mount_favorite_key (mount_key)
);

CREATE TABLE native_mount_backfill_state (
  job_key       VARCHAR(64) CHARACTER SET ascii NOT NULL,
  cursor_guid   INT UNSIGNED NOT NULL DEFAULT 0,
  completed_at  TIMESTAMP NULL,
  PRIMARY KEY (job_key)
);
```

Do not foreign-key `account_id` across databases or `mount_key` to a world table. Validate catalog keys in service code. The primary keys make acquisition/favorite updates idempotent.

If character-specific ownership is configured, do not duplicate all known mounts into a second ownership table; query the loaded spell map. Only favorites require module persistence.

### 9.2 World/content data

The standard catalog should be generated source data, not mutable operator SQL. Resolved custom contributions may use a Content Manager-owned lifecycle table such as:

```sql
content_native_mount (
  package_key, package_version, mount_symbol,
  mount_key, public_spell_id, preview_creature_id, source_item_id,
  category, faction_mask, class_mask,
  required_skill_id, required_skill_rank, required_level,
  source_text, description, flags,
  PRIMARY KEY (package_key, mount_symbol)
)
```

Content Manager, not individual gameplay modules, should own replacement/removal semantics for this table. `mod-native-mounts` consumes it read-only.

## 10. Client/server communication design

Suggested protocol shape, subject to addon-message byte limits:

- `HELLO requestId`: negotiate protocol/catalog revision.
- `PAGE requestId cursor filter category search`: request public entries plus server ownership/usability/favorite state.
- `PAGEINFO requestId nonce cursor next total owned count`
- repeated compact `ENTRY requestId nonce index keyId spellId previewCreature flags ...`
- `END requestId nonce`
- `DETAIL requestId nonce index`: fetch longer source/description separately.
- `FAVORITE requestId nonce index desired`
- `SUMMON requestId nonce index`
- `RANDOM requestId favoritesOnly`
- `RESULT requestId operation code [reason]`

Security properties:

- The client sends a snapshot index, not an authoritative spell/account/mount identity.
- The server snapshot binds index to logical key, account, filters, and expiry.
- Summon rechecks ownership and current eligibility at execution time.
- Favorite mutation rechecks ownership if favorites are collected-only.
- The server resolves the current faction/class variant.
- Normal spell cast performs dynamic combat/movement/map/water/flying/form checks.
- Requests are size/count/rate bounded and malformed packets are consumed silently or return stable errors.

The public catalog may be partially duplicated in client Lua for fast static text, but it cannot be the ownership or summon authority. Server paging is preferable because it naturally includes custom installed content and one catalog revision.

## 11. EPF integration requirements

`mod-native-mounts` should mirror transmog's deterministic packaging pattern:

1. Package XML/Lua/original art as `content/client/...` members.
2. Pin the exact verified stock `FrameXML.toc` SHA-256 used by the cumulative build.
3. Add `clientFrameXml.loadEntries` for `NativeMountsFrame.xml` after `Interface/FrameXML/ContentManagerCollections.lua`.
4. Add `nativeCollectionPages` with unique `id: "mounts"`, explicit order, dimensions, frame, and callbacks.
5. Build deterministic `.epf`; run a `--check` parity test.
6. Install it through Content Manager, compose a new cumulative MPQ, activate explicitly, and update the stable Portalkeeper `realm-content` URL/SHA-256.
7. Never ship the retail screenshot or extracted proprietary retail UI art.

No direct `FrameXML.toc` ownership and no raw overwrite of `ContentManagerCollections.lua` are permitted. Content Manager already rejects those conflicts.

## 12. Technical limitations and risks

1. **Catalog correctness is the largest data risk.** Aura-only discovery produces hundreds of false/duplicate candidates. Make unreviewed delta failure part of CI.
2. **Ownership versus achievement mismatch.** Independent account ownership does not satisfy stock character mount achievements. This must be a documented product decision.
3. **Eligibility is distributed.** Source item, skill-line, trainer, conditions, script code, and faction conversion all carry requirements. Curated overrides are unavoidable.
4. **Server-initiated unlearned normal cast needs PTR proof.** The core path supports normal spell preparation, but verify cast bar, failure messages, cooldown behavior, and client state for a spell absent from the spellbook.
5. **Unseen/custom `SetCreature` needs PTR proof.** Verify server creature-query/cache behavior and patched display/model lookup for an unlearned, never-encountered mount.
6. **No arbitrary rider preview.** The exposed model API cannot attach the player's model to a selected mount. Scope the UI to creature-only preview.
7. **Model framing varies.** Large dragons, vehicles, carpets, and tiny mounts need category/per-entry framing; 3-D widgets can escape clipping.
8. **Addon-chat bandwidth.** Long descriptions cannot travel with every row. Page compact records and fetch selected detail separately.
9. **Account-ID lifecycle.** Account rows in a character DB lack an auth-DB foreign key. Define deletion/reuse operations before production.
10. **Cross-realm semantics.** Independent character DBs imply independent collections. Do not claim global account-wide behavior.
11. **Custom-content lifecycle.** Without a Content Manager typed mount declaration, custom EPF registration is manual and can become stale.
12. **Patch replacement discipline.** Changing Portalkeeper patch key instead of URL/hash can leave multiple cumulative UI/DBC MPQs active.
13. **Localization.** DBC names are localized, but curated source/description text needs an explicit locale strategy and fallback.

## 13. Testing requirements

### Automated

- catalog generator golden file and unreviewed-delta failure;
- every accepted public spell resolves and every variant belongs to exactly one logical key;
- mounted effect/wrapper, creature template, display, model, and icon chain validation;
- known false-positive exclusion fixtures;
- faction-pair and wrapper/internal-spell fixtures;
- ownership primary-key idempotence and concurrent duplicate acquisition;
- login/learn/offline backfill reconciliation;
- forget/revoke policy;
- character deletion and account persistence;
- eligibility matrix: team, race, class, profession, riding, level, Cold Weather Flying;
- protocol malformed/oversized/rate-limit/snapshot-expiry tests;
- client Lua structural/runtime tests following transmog's test style;
- EPF deterministic `--check`, Content Manager cumulative composition, uninstall/replacement, and generated resource parity;
- Portalkeeper stable-slot update/rollback validation.

### PTR/manual build-12340

- all UI scales and common resolutions;
- search/filter/count and large-page scrolling;
- collected/uncollected/favorite states after live changes;
- creature preview for ground, dragon, carpet, vehicle/passenger, aquatic, and custom mount;
- rotation and scale reset between radically different models;
- unlearned/unseen model preview;
- summon success/failure in combat, moving, water, dungeon, arena, no-fly area, Northrend without Cold Weather Flying, disallowed forms, and on another mount;
- profession/faction/class unusable reasons;
- adaptive mounts at each riding tier;
- two same-account online characters receiving collection invalidation;
- relog/restart and Portalkeeper patch replacement.

## 14. Recommended implementation phases

1. **Catalog proof and PTR model spike**
   - implement no module behavior;
   - generate candidates, review identity rules, and prove `SetCreature`/framing on representative mounts and one unlearned mount.
2. **Independent Collections page shell**
   - Mounts EPF page registration, static fixture list, search/list/detail/model UI;
   - verify coexistence with Appearances and cumulative Content Manager packaging.
3. **Character-specific read-only journal**
   - server catalog/protocol; ownership from `HasSpell`; counters/filtering; no summon mutation yet.
4. **Secure summon of already-known spells**
   - snapshot protocol and normal cast path; validate all core restrictions and errors.
5. **Account ownership with eligibility**
   - schema, learn/login/offline reconciliation, unusable reasons, account cache; keep auto-teach off.
6. **Favorites and random favorite**
   - server persistence and eligible server-side random choice.
7. **Custom catalog registration**
   - Content Manager typed manifest/server-bundle extension and replacement lifecycle.
8. **Polish/localization/data completion**
   - original descriptions/source metadata, framing overrides, accessibility, performance, and final catalog review.

Each phase should retain a usable fallback and be separately deployable/testable. Account migration should not begin until character-specific catalog identity is stable; changing logical keys after account collection launch requires a data migration.

## 15. Open architectural decisions

1. Should faction-equivalent spells count as one logical account mount or two WotLK spell identities? Recommendation: one account key, one team-selected variant, plus a separate spell-count diagnostic.
2. Should stock mount achievements remain character-specific? Recommendation: yes initially; do not silently redefine core achievements.
3. Should forgetting a spell revoke account ownership? Recommendation: no; only explicit administrative revoke.
4. Should favorites be account- or character-scoped? Recommendation: account by default, configurable only if there is a concrete UX need.
5. Should uncollected faction-ineligible mounts be visible? Recommendation: yes with a faction indicator; filters can hide unusable entries.
6. Should source descriptions be server data or client-localized data? Recommendation: catalog data with locale overlays and English fallback; detail fetched on selection.
7. Is server casting an account-owned but unlearned public spell acceptable after PTR validation, or must eligible spells be materialized into the spellbook? Recommendation: server cast; add materialization only as an explicit later compatibility mode with a grant ledger.
8. Is cross-realm account ownership required? If yes, the database/service boundary must be decided before schema finalization; it is not an incremental flag on the proposed local character-DB table.
9. Will custom mount packages need semantic creature display/model allocation? If yes, plan the Content Manager resource type before promising EPF-only custom mount registration.
10. What is the supported catalog cutoff: original Blizzard 3.3.5a obtainable mounts only, all legitimate client-resident player mounts, or realm-enabled retired/promotional mounts? The curated allowlist and collection denominator depend on this product decision.

## Final recommendation

Proceed with a `mod-native-mounts` module that is independent of transmog and contributes a sibling **Mounts** page to the Content Manager-owned Collections host. Build a deterministic reviewed catalog from `SkillLineAbility` skill 777 plus mounted-aura/wrapper, source-item, faction-change, and server relationship data. Use a single creature-only `PlayerModel` preview. Begin with character spellbook ownership, then add account ownership with character-specific eligibility and no default auto-teaching. Keep the client untrusted, authorize every operation from the authenticated server session, and invoke the ordinary non-triggered AzerothCore spell path for final summon rules.

No AzerothCore core change is presently justified. The only likely shared-system extension is an additive Content Manager mount-catalog resource for custom EPF content.
