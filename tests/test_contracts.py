#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class PocContracts(unittest.TestCase):
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
        self.assertIn("PickupSpell(button.spellBookSlot, BOOKTYPE_SPELL)", lua)
        self.assertNotIn("PlaceAction", lua)

    def test_restricted_fixtures_are_present(self):
        catalog = (ROOT / "src/NativeMountCatalog.cpp").read_text()
        for spell in (58983, 59567, 23161, 44151, 32243):
            self.assertRegex(catalog, rf"\b{spell}\b")

    def test_only_ledger_rows_are_removed(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        removal = service.index("player->removeSpell")
        preceding = service[max(0, removal - 800):removal]
        self.assertIn("moduleGrant", preceding)
        self.assertIn("revokeIneligible", preceding)

    def test_external_unlearn_has_suppression_tombstone(self):
        service = (ROOT / "src/NativeMountService.cpp").read_text()
        self.assertIn("`state`=4", service)
        self.assertIn("externallySuppressed", service)
        self.assertIn("AND `state`=1", service)
        schema = (ROOT / "data/sql/db-characters/base/001_native_mounts_poc.sql").read_text()
        self.assertIn("administratively revoked tombstone", schema)


if __name__ == "__main__":
    unittest.main()
