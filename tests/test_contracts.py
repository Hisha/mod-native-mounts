#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class CatalogContracts(unittest.TestCase):
    def test_collection_page_is_independent(self):
        manifest = json.loads((ROOT / "content/manifest.json").read_text())
        self.assertEqual(manifest["nativeCollectionPages"][0]["id"], "mounts")
        self.assertEqual(manifest["nativeCollectionPages"][0]["frame"], "NativeMountsCollectionsFrame")
        self.assertNotIn("transmog", (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text().lower())

    def test_client_inputs_parse_and_pin_stock_toc(self):
        ET.parse(ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.xml")
        manifest = json.loads((ROOT / "content/manifest.json").read_text())
        toc = (ROOT / "content" / manifest["clientFrameXml"]["stockTocSource"]).read_bytes()
        self.assertEqual(hashlib.sha256(toc).hexdigest(), manifest["clientFrameXml"]["stockTocSha256"])

    def test_drag_payload_is_native_spellbook_action(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        row_drag = lua[lua.index("function NativeMounts_Row_OnDragStart"):
                       lua.index("function NativeMounts_Row_OnClick")]
        self.assertIn('PickupCompanion("MOUNT", slot)', row_drag)
        self.assertIn("mountCompanionSlot(button.variant.spell)", row_drag)
        self.assertNotIn("PlaceAction", row_drag)
        self.assertNotIn("PickupSpell", row_drag)

    def test_spellbook_resolution_uses_exact_spell_ids(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        resolver = lua[lua.index("local function mountCompanionSlot"):
                       lua.index("local function spellBookSlot")]
        self.assertIn('GetNumCompanions("MOUNT")', resolver)
        self.assertIn('GetCompanionInfo("MOUNT", slot)', resolver)
        self.assertIn("companionSpellID == spellID", resolver)
        self.assertNotIn("GetSpellName", resolver)
        self.assertNotIn("GetSpellLink", resolver)
        self.assertNotIn("GetSpellInfo", resolver)

    def test_companion_slot_is_resolved_before_native_pickup(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertIn("local slot = mountCompanionSlot(variant.spell)", lua)
        self.assertIn("button.companionSlot = slot", lua)
        self.assertLess(lua.index("button.companionSlot = slot"),
                        lua.index('PickupCompanion("MOUNT", slot)'))

    def test_restricted_fixtures_are_present(self):
        catalog = (ROOT / "generated/NativeMountCatalog.inc").read_text()
        for spell in (58983, 59567, 23161, 44151, 32243):
            self.assertRegex(catalog, rf"\b{spell}\b")

    def test_client_catalog_uses_server_spell_ids(self):
        data = json.loads((ROOT / "generated/mount_catalog.json").read_text())
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountCatalog.lua").read_text()
        server_ids = {row["spell"] for row in data["mounts"]}
        client_ids = {int(value) for value in re.findall(r'spell=(\d+),', lua)}
        self.assertEqual(client_ids, server_ids)
        self.assertIn(58983, client_ids)
        self.assertEqual(lua.count(" {key="), data["statistics"]["logicalMounts"])

    def test_only_ledger_rows_are_removed(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        removal = service.index("player->removeSpell")
        decision = service[service.index("DecideReconcileAction"):removal]
        self.assertIn("moduleGrant", decision)
        self.assertIn("revokeIneligible", decision)
        self.assertIn("ReconcileAction::Revoke", decision)

    def test_external_unlearn_has_suppression_tombstone(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertIn("`state`=4", service)
        self.assertIn("externallySuppressed", service)
        self.assertIn("ownershipIt->second == 1", service)
        schema = (ROOT / "data/sql/db-characters/base/001_native_mounts_poc.sql").read_text()
        self.assertIn("administratively revoked tombstone", schema)

    def test_reconciliation_is_event_driven_with_no_idle_path(self):
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        header = (ROOT / "src/NativeMountService.h").read_text()
        config = (ROOT / "conf/mod_native_mounts.conf.dist").read_text()
        self.assertNotIn("PLAYERHOOK_ON_UPDATE,", module)
        self.assertNotIn("void OnPlayerUpdate(", module)
        self.assertNotIn("ReconcileInterval", module + service + header + config)
        self.assertNotIn("_elapsed", service + header)
        for hook in ("PLAYERHOOK_ON_LOGIN", "PLAYERHOOK_ON_BEFORE_LOGOUT",
                     "PLAYERHOOK_ON_UPDATE_SKILL", "PLAYERHOOK_ON_SET_SKILL",
                     "PLAYERHOOK_ON_LEVEL_CHANGED"):
            self.assertNotIn(hook, module)
        self.assertIn("PLAYERHOOK_ON_LEARN_SPELL", module)
        self.assertIn("PLAYERHOOK_ON_FORGOT_SPELL", module)

    def test_collections_uses_one_batched_state_read(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        load = service[service.index("ReconcileState LoadState"):service.index("bool IsModuleGrant")]
        self.assertEqual(load.count("CharacterDatabase.Query("), 1)
        self.assertIn("UNION ALL SELECT", load)
        self.assertIn("`mount_key` IN ({})", load)
        reconcile = service[service.index("void Service::ReconcileCollections"):service.index("void Service::HandleLearn")]
        self.assertIn("LoadState(player, variants)", reconcile)
        self.assertNotIn("CharacterDatabase.Query(", reconcile)

    def test_acquisition_and_admin_changes_do_not_fan_out(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        self.assertNotIn("ReconcileAccountKey", service)
        self.assertNotIn("ObjectAccessor", service)
        learn = service[service.index("void Service::HandleLearn"):service.index("void Service::HandleForget")]
        self.assertIn("INSERT IGNORE INTO `native_mount_account_ownership`", learn)
        self.assertNotIn("CharacterDatabase.Query(", learn)
        self.assertIn("native-mounts", module)
        self.assertIn("administratively revoked", module)

    def test_login_population_has_zero_mount_work(self):
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertNotIn("OnPlayerLogin", module)
        self.assertNotIn("ReconcileLogin", module + service)
        # Scaling from one to 1,000 login events cannot invoke module code when
        # no login callback is registered.
        self.assertEqual(sum("OnPlayerLogin" in module for _ in range(1000)), 0)

    def test_explicit_versioned_request_reconciles_and_rejects_stale_responses(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertIn('VERSION .. "\\tSYNC\\t" .. activeRequestId', lua)
        self.assertIn("ReconcileCollections(player, requestId)", service)
        self.assertIn("tonumber(request) ~= activeRequestId", lua)
        self.assertIn('"NMOUNTS\\t1\\tEND\\t" + std::to_string(requestId)', service)

    def test_uncollected_preview_primes_creature_cache_and_clears_stale_model(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        selection = lua[lua.index("local function selectEntry"):lua.index("local function refreshButton")]
        self.assertLess(selection.index("ClearModel()"), selection.index("SetCreature(variant.creature)"))
        self.assertIn("PrimePreviewCreatureCache(player)", service)
        self.assertIn("HandleCreatureQueryOpcode(query)", service)
        self.assertNotIn("learnSpell", selection)
        self.assertNotIn("CastSpell", selection)

        data = json.loads((ROOT / "generated/mount_catalog.json").read_text())
        fixtures = {row["name"]: row["previewCreature"] for row in data["mounts"]
                    if row["name"] in {"Brown Ram", "White Ram", "Brown Elekk"}}
        self.assertEqual(set(fixtures), {"Brown Ram", "White Ram", "Brown Elekk"})
        self.assertTrue(all(fixtures.values()))

    def test_faction_variant_selection_prefers_current_character(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        chooser = lua[lua.index("local function chooseVariant"):lua.index("local function entryOwned")]
        eligible_known = chooser.index("mountCompanionSlot(variant.spell) then return variant end")
        eligible = chooser.index("maskHas(variant.races, raceId)", eligible_known + 1)
        fallback_known = chooser.rindex("mountCompanionSlot(variant.spell) then return variant end")
        self.assertLess(eligible_known, eligible)
        self.assertLess(eligible, fallback_known)

    def test_collections_refreshes_on_spell_events(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertIn('RegisterEvent("SPELLS_CHANGED")', lua)
        self.assertIn('RegisterEvent("COMPANION_LEARNED")', lua)
        self.assertIn('RegisterEvent("COMPANION_UNLEARNED")', lua)
        self.assertIn('RegisterEvent("COMPANION_UPDATE")', lua)
        event_handler = lua[lua.index('frame:SetScript("OnEvent"'):lua.index('frame:EnableMouseWheel')]
        self.assertIn("rebuildFiltered(false)", event_handler)

    def test_filtering_is_local_logical_and_preserves_selection_contract(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        filtering = lua[lua.index("local function rebuildFiltered"):lua.index("local function setOffset")]
        self.assertIn('currentFilter == "ALL"', filtering)
        self.assertIn('currentFilter == "COLLECTED" and owned', filtering)
        self.assertIn('currentFilter == "UNCOLLECTED" and not owned', filtering)
        self.assertIn("entry.key == selectedKey", filtering)
        self.assertIn("clearPreview()", filtering)
        self.assertNotIn("SendAddonMessage", filtering)
        self.assertIn("if resetScroll then offset = 0", filtering)
        self.assertIn("#filteredCatalog - ROWS", filtering)

    def test_counter_classifies_account_owned_logical_entries(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        counter = lua[lua.index("local function updateCount"):lua.index("local function refreshRows")]
        self.assertIn("for _, entry in ipairs(CATALOG)", counter)
        self.assertIn("entryOwned(entry)", counter)
        self.assertIn("%d total  |  %d collected  |  %d uncollected", counter)
        data = json.loads((ROOT / "generated/mount_catalog.json").read_text())
        self.assertEqual(data["statistics"]["logicalMounts"], 192)

    def test_random_mount_is_managed_native_spell_action(self):
        manifest = json.loads((ROOT / "content/manifest.json").read_text())
        spell = next(row for row in manifest["spells"] if row["symbol"] == "random-mount")
        self.assertEqual(spell["profile"], "stock-spell-clone-v1")
        # Build 12340 donor 5409 uses CastingTimeIndex 1 (instant). Donor 5407
        # uses index 14 and produces the unwanted dispatcher cast bar.
        self.assertEqual(spell["copyFrom"], 5409)
        self.assertEqual(spell["iconCopyFromSpell"], 48025)
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertIn('CreateFrame("Button", "NativeMountsRandomButton", frame, "SecureActionButtonTemplate")', lua)
        self.assertIn("PickupSpell(slot, BOOKTYPE_SPELL)", lua)
        self.assertIn('SetAttribute("type", name and "spell" or nil)', lua)
        self.assertNotIn("CreateMacro", lua)
        self.assertNotIn("EditMacro", lua)

    def test_random_mount_spellbook_lookup_uses_build_12340_api(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertNotIn("PickupSpellBookItem", lua)
        self.assertNotIn("GetSpellBookItemInfo", lua)
        resolver = lua[lua.index("local function spellBookSlot"):
                       lua.index("local function maskHas")]
        self.assertIn("GetSpellInfo(spellID)", resolver)
        self.assertIn("GetSpellTabInfo(tab)", resolver)
        self.assertIn("for slot = offset + 1, offset + count do", resolver)
        self.assertIn("GetSpellName(slot, BOOKTYPE_SPELL)", resolver)
        self.assertNotIn("GetSpellBookItemInfo", resolver)

        drag = lua[lua.index('randomButton:SetScript("OnDragStart"'):
                   lua.index('randomButton:SetScript("OnEnter"')]
        self.assertIn("spellBookSlot(randomMountSpell)", drag)
        self.assertIn("PickupSpell(slot, BOOKTYPE_SPELL)", drag)
        self.assertNotIn("PickupSpellBookItem", drag)
        self.assertNotIn("GetSpellBookItemInfo", drag)

    def test_random_mount_uses_current_spells_and_normal_cast_checks(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        chooser = service[service.index("SpellCastResult Service::PrepareRandomMount"):
                          service.index("void Service::CastPreparedRandomMount")]
        self.assertIn("player->HasSpell(variant.spellId)", chooser)
        self.assertIn("Evaluate(variant, Snapshot(player, variant))", chooser)
        self.assertIn("LoadState(player, known)", chooser)
        self.assertIn("probe.CheckCast(true) == SPELL_CAST_OK", chooser)
        self.assertIn("logicalKeys.insert(variant->key)", chooser)
        cast = service[service.index("void Service::CastPreparedRandomMount"):
                       service.index("void Service::CancelPreparedRandomMount")]
        self.assertIn("player->CastSpell(player, spellId, false)", cast)
        self.assertNotIn("learnSpell", chooser + cast)
        self.assertIn("NativeMountRandomSpellScript", module)

    def test_random_mount_dispatcher_is_instant_but_selected_mount_is_not_triggered(self):
        manifest = json.loads((ROOT / "content/manifest.json").read_text())
        spell = next(row for row in manifest["spells"] if row["symbol"] == "random-mount")
        self.assertEqual(spell["copyFrom"], 5409)
        self.assertNotEqual(spell["copyFrom"], 5407)
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        cast = service[service.index("void Service::CastPreparedRandomMount"):
                       service.index("void Service::CancelPreparedRandomMount")]
        self.assertIn("player->CastSpell(player, spellId, false)", cast)
        self.assertNotIn("player->CastSpell(player, spellId, true)", cast)

    def test_random_mount_spell_handler_is_registered_for_cast_paths(self):
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        script = module[module.index("class NativeMountRandomSpellScript"):
                        module.index("class NativeMountCommandScript")]
        registration = module[module.index("void AddNativeMountModuleScripts") :]
        self.assertIn("OnSpellCheckCast", script)
        self.assertIn("PrepareRandomMount", script)
        self.assertIn("OnSpellCast", script)
        self.assertIn("CastPreparedRandomMount", script)
        self.assertIn("OnSpellCastCancel", script)
        self.assertIn("CancelPreparedRandomMount", script)
        self.assertIn("new NativeMountRandomSpellScript();", registration)

    def test_random_mount_has_no_login_or_polling_path(self):
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertNotIn("PLAYERHOOK_ON_LOGIN", module)
        self.assertNotIn("OnPlayerLogin", module)
        self.assertNotIn('SetScript("OnUpdate"', lua)

    def test_collections_icon_switches_only_for_mounts(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        icon = lua[lua.index("local function setCollectionsIcon"):
                   lua.index("function NativeMounts_Row_OnDragStart")]
        self.assertIn('Interface\\\\Minimap\\\\Tracking\\\\StableMaster', lua)
        self.assertNotIn('Interface\\\\Buttons\\\\UI-Quickslot2', icon)
        self.assertNotIn('Interface\\\\Icons\\\\Ability_Mount_RidingHorse', icon)
        self.assertIn('setCollectionsIcon(true)', lua)
        self.assertIn('setCollectionsIcon(false)', lua)
        self.assertIn('SetPortraitToTexture(collectionsMountField,', icon)
        self.assertIn('SetPortraitToTexture(collectionsMountIcon, COLLECTIONS_MOUNT_ICON)', icon)
        self.assertIn('collectionsMountField:Show()', icon)
        self.assertIn('collectionsMountField:Hide()', icon)
        self.assertIn('collectionsMountIcon:Show()', icon)
        self.assertIn('collectionsMountIcon:Hide()', icon)
        self.assertNotIn('SetAllPoints(iconFrame)', icon)

    def test_random_mount_button_has_one_cropped_icon_and_native_button_states(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        button = lua[lua.index('randomButton = CreateFrame'):
                     lua.index('randomButton.label =')]
        self.assertEqual(button.count('randomButton.icon = randomButton:CreateTexture'), 1)
        self.assertIn('randomButton.icon:SetTexCoord(.07, .93, .07, .93)', button)
        self.assertIn('randomButton:SetNormalTexture("Interface\\\\Buttons\\\\UI-Quickslot2")', button)
        self.assertIn('randomButton:SetPushedTexture("Interface\\\\Buttons\\\\UI-Quickslot-Depress")', button)
        self.assertIn('randomButton:SetHighlightTexture("Interface\\\\Buttons\\\\ButtonHilight-Square", "ADD")', button)
        self.assertNotIn('randomButton.border', button)
        configure = lua[lua.index("local function configureRandomButton"):
                        lua.index("local function setCollectionsIcon")]
        self.assertIn("randomButton.icon:SetDesaturated(false)", configure)
        self.assertIn("randomButton.icon:SetDesaturated(true)", configure)

    def test_client_has_no_periodic_refresh_or_server_query(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertNotIn('SetScript("OnUpdate"', lua)
        self.assertNotIn("C_Timer", lua)
        self.assertEqual(lua.count("SendAddonMessage(PREFIX"), 1)
        self.assertIn('frame:RegisterEvent("CHAT_MSG_ADDON")', lua)

    def test_generated_catalog_is_complete_and_unique(self):
        data = json.loads((ROOT / "generated/mount_catalog.json").read_text())
        mounts = data["mounts"]
        self.assertGreater(len(mounts), 200)
        self.assertEqual(len({row["spell"] for row in mounts}), len(mounts))
        self.assertEqual([row["spell"] for row in mounts], sorted(row["spell"] for row in mounts))
        self.assertEqual(data["statistics"]["acceptedWithoutResolvedMountedAura"], [])
        self.assertEqual(data["statistics"]["candidateMountedAuraSpells"], 396)

    def test_account_ownership_is_not_inferred_from_spellbook(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertIn("entryOwned(entry)", lua)
        self.assertIn("native_mount_account_ownership", service)
        self.assertIn("SendSnapshot(player, owned, requestId,", service)

    def test_profession_uses_trained_value(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertIn("GetPureSkillValue(variant.professionSkill)", service)
        self.assertNotIn("result.professionRank = player->GetSkillValue", service)


if __name__ == "__main__":
    unittest.main()
