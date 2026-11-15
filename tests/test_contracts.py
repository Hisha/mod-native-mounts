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
        self.assertIn('PickupCompanion("MOUNT", slot)', lua)
        self.assertIn("mountCompanionSlot(button.variant.spell)", lua)
        self.assertNotIn("PlaceAction", lua)
        self.assertNotIn("PickupSpell", lua)

    def test_spellbook_resolution_uses_exact_spell_ids(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        resolver = lua[lua.index("local function mountCompanionSlot"):lua.index("local function refreshButton")]
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
        self.assertIn("PLAYERHOOK_ON_UPDATE_SKILL", module)
        self.assertIn("PLAYERHOOK_ON_SET_SKILL", module)
        self.assertIn("PLAYERHOOK_ON_LEVEL_CHANGED", module)

    def test_login_uses_one_batched_state_read(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        load = service[service.index("ReconcileState LoadState"):service.index("bool IsModuleGrant")]
        self.assertEqual(load.count("CharacterDatabase.Query("), 1)
        self.assertIn("UNION ALL SELECT", load)
        self.assertIn("`mount_key` IN ({})", load)

    def test_acquisition_and_admin_changes_fan_out_by_key(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        module = (ROOT / "src/NativeMountModule.cpp").read_text()
        self.assertIn("ReconcileAccountKey(account, variant->key)", service)
        self.assertIn("ReconcileAccountKey(accountId, mountKey)", service)
        self.assertIn("ObjectAccessor::FindPlayerByLowGUID", service)
        self.assertIn("native-mounts", module)
        self.assertIn("administratively revoked", module)

    def test_collections_refreshes_on_spell_events(self):
        lua = (ROOT / "content/client/Interface/FrameXML/NativeMountsFrame.lua").read_text()
        self.assertIn('RegisterEvent("SPELLS_CHANGED")', lua)
        self.assertIn('RegisterEvent("COMPANION_LEARNED")', lua)
        self.assertIn('RegisterEvent("COMPANION_UNLEARNED")', lua)
        self.assertIn('RegisterEvent("COMPANION_UPDATE")', lua)
        event_handler = lua[lua.index('frame:SetScript("OnEvent"'):lua.index('frame:EnableMouseWheel')]
        self.assertIn("refreshRows()", event_handler)

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
        self.assertIn("SendOwnershipSnapshot", service)

    def test_profession_uses_trained_value(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertIn("GetPureSkillValue(variant.professionSkill)", service)
        self.assertNotIn("result.professionRank = player->GetSkillValue", service)


if __name__ == "__main__":
    unittest.main()
