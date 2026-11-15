from pathlib import Path
import importlib.util
import json
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("mount_generator", ROOT / "tools/generate_mount_catalog.py")
GEN = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GEN
SPEC.loader.exec_module(GEN)


def spell(spell_id, name="Custom Mount", aura=78, creature=90001):
    return GEN.Spell(spell_id, name, 42, 20, (6, 0, 0), (aura, 0, 0),
                     (creature, 0, 0), (0, 0, 0))


class GeneratorTests(unittest.TestCase):
    def test_custom_durable_mount_is_discovered_without_registration(self):
        custom = 90000
        spells = {custom: spell(custom)}
        items = [dict(item=90002, itemName="Reins", spells=[custom], level=20,
                      skill=762, skillRank=75, requiredSpell=0, races=-1, classes=-1)]
        catalog, _, _ = GEN.build_catalog(spells, items, [])
        self.assertEqual([row["spell"] for row in catalog], [custom])
        self.assertEqual(catalog[0]["previewCreature"], 90001)
        self.assertEqual(catalog[0]["minimumRiding"], 75)

    def test_temporary_direct_cast_item_is_not_an_input_candidate(self):
        # read_items only accepts trigger 6; build_catalog receives no source
        # for a temporary direct-cast spell and therefore cannot admit it.
        catalog, _, _ = GEN.build_catalog({90003: spell(90003)}, [], [])
        self.assertEqual(catalog, [])

    def test_rendering_is_deterministic(self):
        data = json.loads((ROOT / "generated/mount_catalog.json").read_text())
        catalog = data["mounts"]
        digest = data["statistics"]["sourceSha256"]
        self.assertEqual(GEN.render_cpp(catalog, digest), GEN.render_cpp(catalog, digest))
        self.assertEqual(GEN.render_lua(catalog, digest), GEN.render_lua(catalog, digest))

    def test_faction_equivalents_share_one_stable_key(self):
        spells = {100: spell(100), 200: spell(200)}
        source = lambda ident: dict(item=ident + 1000, itemName="Reins", spells=[ident],
                                    level=20, skill=762, skillRank=75, requiredSpell=0,
                                    races=-1, classes=-1)
        catalog, _, _ = GEN.build_catalog(spells, [source(100), source(200)], [(100, 200)])
        self.assertEqual(len({row["key"] for row in catalog}), 1)


if __name__ == "__main__":
    unittest.main()
