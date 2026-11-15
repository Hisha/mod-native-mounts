#!/usr/bin/env python3
"""Generate the native-mount catalog from effective build-12340 data.

Stock discovery uses durable mount teaching items (item class 15/subclass 5,
ITEM_SPELLTRIGGER_LEARN_SPELL_ID) plus the small trainer-only overlay below.
The input Spell.dbc must be the effective, already-composed client DBC.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

MOUNTED_AURA = 78
LEARN_SPELL_TRIGGER = 6
ALL_RACES = 0x7FFF
ALL_CLASSES = 0x3FFFF
RIDING_SKILL = 762
RIDING_SPELL_RANKS = {33388: 75, 33391: 150, 34090: 225, 34091: 300}

# These public mount spells are learned from trainers/quests rather than a
# durable mount item. This is the only stock include overlay.
TRAINER_MOUNTS = {
    5784: dict(level=20, riding=75, classes=1 << 8, name="Felsteed"),
    13819: dict(level=20, riding=75, classes=1 << 1, races=0x44D, name="Warhorse"),
    23161: dict(level=40, riding=150, classes=1 << 8, name="Dreadsteed"),
    23214: dict(level=40, riding=150, classes=1 << 1, races=0x44D, name="Charger"),
    34767: dict(level=40, riding=150, classes=1 << 1, races=0x2B2, name="Summon Charger"),
    34769: dict(level=20, riding=75, classes=1 << 1, races=0x2B2, name="Summon Warhorse"),
    48778: dict(level=55, riding=0, classes=1 << 5, name="Acherus Deathcharger"),
}

LEGACY_KEYS = {
    58983: "big-blizzard-bear",
    59567: "azure-drake",
    23161: "warlock-dreadsteed",
    44151: "turbo-flying-machine",
    32243: "tawny-wind-rider",
}


@dataclass(frozen=True)
class Spell:
    spell_id: int
    name: str
    icon: int
    level: int
    effects: tuple[int, int, int]
    auras: tuple[int, int, int]
    misc: tuple[int, int, int]
    triggers: tuple[int, int, int]

    @property
    def preview_creature(self) -> int:
        for aura, value in zip(self.auras, self.misc):
            if aura == MOUNTED_AURA and value > 0:
                return value
        return 0


def read_dbc(path: Path) -> tuple[list[list[int]], bytes]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"WDBC":
        raise ValueError(f"{path}: expected a WDBC file")
    records, fields, record_size, strings_size = struct.unpack_from("<4I", data, 4)
    if record_size != fields * 4:
        raise ValueError(f"{path}: unsupported non-32-bit DBC fields")
    end = 20 + records * record_size
    if end + strings_size != len(data):
        raise ValueError(f"{path}: inconsistent WDBC header")
    rows = [list(struct.unpack_from(f"<{fields}I", data, 20 + i * record_size))
            for i in range(records)]
    return rows, data[end:]


def dbc_string(block: bytes, offset: int) -> str:
    if offset >= len(block):
        return ""
    end = block.find(b"\0", offset)
    return block[offset:end if end >= 0 else None].decode("utf-8", "replace")


def read_spells(path: Path) -> dict[int, Spell]:
    rows, strings = read_dbc(path)
    if not rows or len(rows[0]) != 234:
        raise ValueError(f"{path}: build-12340 Spell.dbc must contain 234 fields")
    result = {}
    for row in rows:
        signed = lambda value: value if value < 0x80000000 else value - 0x100000000
        spell = Spell(row[0], dbc_string(strings, row[136]), row[133], row[39],
                      tuple(row[71:74]), tuple(row[95:98]),
                      tuple(signed(v) for v in row[110:113]), tuple(row[116:119]))
        result[spell.spell_id] = spell
    return result


def sql_columns(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    match = re.search(r"CREATE TABLE `item_template` \((.*?)\n\)", text, re.S)
    if not match:
        raise ValueError(f"{path}: item_template schema not found")
    return re.findall(r"^\s*`([^`]+)`", match.group(1), re.M)


def sql_tuples(path: Path):
    """Yield INSERT tuples while honoring MySQL quotes and backslash escapes."""
    text = path.read_text(encoding="utf-8")
    for insert in re.finditer(r"INSERT INTO `[^`]+` VALUES\s*", text):
        pos = insert.end()
        while pos < len(text) and text[pos] == "(":
            pos += 1
            values, token, quoted, escaped = [], [], False, False
            while pos < len(text):
                char = text[pos]
                pos += 1
                if escaped:
                    token.append(char); escaped = False; continue
                if quoted and char == "\\":
                    escaped = True; continue
                if char == "'":
                    quoted = not quoted; continue
                if not quoted and char in ",)":
                    raw = "".join(token).strip()
                    values.append(None if raw == "NULL" else raw)
                    token = []
                    if char == ")":
                        break
                    continue
                token.append(char)
            yield values
            while pos < len(text) and text[pos].isspace(): pos += 1
            if pos < len(text) and text[pos] == ",":
                pos += 1
                while pos < len(text) and text[pos].isspace(): pos += 1
            else:
                break


def integer(value, default=0) -> int:
    return default if value in (None, "") else int(value)


def read_items(path: Path) -> list[dict]:
    columns = sql_columns(path)
    result = []
    for values in sql_tuples(path):
        if len(values) != len(columns):
            raise ValueError(f"{path}: row has {len(values)} values; expected {len(columns)}")
        row = dict(zip(columns, values))
        if integer(row["class"]) != 15 or integer(row["subclass"]) != 5:
            continue
        learned = [integer(row[f"spellid_{slot}"]) for slot in range(1, 6)
                   if integer(row[f"spelltrigger_{slot}"]) == LEARN_SPELL_TRIGGER]
        if not learned:
            continue
        result.append(dict(item=integer(row["entry"]), itemName=row["name"], spells=learned,
                           level=integer(row["RequiredLevel"]),
                           skill=integer(row["RequiredSkill"]),
                           skillRank=integer(row["RequiredSkillRank"]),
                           requiredSpell=integer(row["requiredspell"]),
                           races=integer(row["AllowableRace"], -1),
                           classes=integer(row["AllowableClass"], -1)))
    return result


def read_faction_pairs(path: Path) -> list[tuple[int, int]]:
    pairs = []
    for row in sql_tuples(path):
        if len(row) >= 4:
            pairs.append((integer(row[0]), integer(row[2])))
    return pairs


def resolves_mount(spell_id: int, spells: dict[int, Spell], seen=None) -> bool:
    if seen is None: seen = set()
    if spell_id in seen or spell_id not in spells: return False
    seen.add(spell_id)
    spell = spells[spell_id]
    return MOUNTED_AURA in spell.auras or any(
        resolves_mount(target, spells, seen) for target in spell.triggers if target)


def build_catalog(spells: dict[int, Spell], items: list[dict], pairs: list[tuple[int, int]]):
    sources: dict[int, list[dict]] = {}
    for item in items:
        for spell_id in item["spells"]:
            sources.setdefault(spell_id, []).append(item)
    accepted = set(sources) | set(TRAINER_MOUNTS)
    accepted = {spell_id for spell_id in accepted if spell_id in spells}

    parent = {spell_id: spell_id for spell_id in accepted}
    def find(value):
        while parent[value] != value:
            parent[value] = parent[parent[value]]; value = parent[value]
        return value
    def union(left, right):
        left, right = find(left), find(right)
        if left != right: parent[max(left, right)] = min(left, right)
    for left, right in pairs:
        if left in accepted and right in accepted: union(left, right)

    groups: dict[int, list[int]] = {}
    for spell_id in accepted: groups.setdefault(find(spell_id), []).append(spell_id)
    group_key = {}
    for members in groups.values():
        legacy = sorted((LEGACY_KEYS[s] for s in members if s in LEGACY_KEYS))
        key = legacy[0] if legacy else (f"wotlk:faction:{min(members)}:{max(members)}"
                                       if len(members) > 1 else f"wotlk:spell:{members[0]}")
        for member in members: group_key[member] = key

    catalog = []
    for spell_id in sorted(accepted):
        spell = spells[spell_id]
        source = sorted(sources.get(spell_id, []), key=lambda row: row["item"])
        if source:
            item = source[0]
            level = item["level"] or spell.level
            races = ALL_RACES if item["races"] == -1 else item["races"]
            classes = ALL_CLASSES if item["classes"] == -1 else item["classes"]
            if item["skill"] == RIDING_SKILL:
                riding, profession, profession_rank = item["skillRank"], 0, 0
            else:
                riding = RIDING_SPELL_RANKS.get(item["requiredSpell"], 0)
                profession = item["skill"]
                profession_rank = item["skillRank"] if profession else 0
            source_item = item["item"]
        else:
            override = TRAINER_MOUNTS[spell_id]
            level, riding = override["level"], override["riding"]
            races, classes = override.get("races", ALL_RACES), override["classes"]
            profession = profession_rank = source_item = 0
        catalog.append(dict(key=group_key[spell_id], spell=spell_id,
                            name=spell.name or TRAINER_MOUNTS.get(spell_id, {}).get("name", ""),
                            icon=spell.icon, previewCreature=spell.preview_creature,
                            minimumLevel=level, minimumRiding=riding,
                            allowedRaceMask=races, allowedClassMask=classes,
                            professionSkill=profession, professionRank=profession_rank,
                            sourceItem=source_item, collectionEligible=True,
                            validation="mounted-aura" if resolves_mount(spell_id, spells)
                            else "durable-mount-teaching-item"))
    return catalog, sources, groups


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def render_cpp(catalog: list[dict], digest: str) -> str:
    lines = ["// Generated by tools/generate_mount_catalog.py; do not edit.",
             f"// source-sha256: {digest}"]
    for row in catalog:
        lines.append("    { %s, %d, %s, %d, %d, %d, %d, 0x%X, 0x%X, %d, %d, %d }," % (
            cpp_string(row["key"]), row["spell"], cpp_string(row["name"]), row["icon"],
            row["previewCreature"], row["minimumLevel"], row["minimumRiding"],
            row["allowedRaceMask"], row["allowedClassMask"], row["professionSkill"],
            row["professionRank"], row["sourceItem"]))
    return "\n".join(lines) + "\n"


def render_lua(catalog: list[dict], digest: str) -> str:
    lines = ["-- Generated by tools/generate_mount_catalog.py; do not edit.",
             f"-- source-sha256: {digest}", "NATIVE_MOUNT_CATALOG = {"]
    groups = {}
    for row in catalog:
        groups.setdefault(row["key"], []).append(row)
    for key in sorted(groups, key=lambda value: min(row["spell"] for row in groups[value])):
        lines.append(' {key="%s",variants={' % key)
        for row in groups[key]:
            name = row["name"].replace("\\", "\\\\").replace('"', '\\"')
            lines.append('  {spell=%d,name="%s",icon=%d,creature=%d,races=%d,classes=%d},' %
                         (row["spell"], name, row["icon"], row["previewCreature"],
                          row["allowedRaceMask"], row["allowedClassMask"]))
        lines.append(" }},")
    lines.append("}")
    return "\n".join(lines) + "\n"


def write_or_check(path: Path, content: str, check: bool):
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise SystemExit(f"generated artifact is stale: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spell-dbc", type=Path, required=True,
                        help="effective/composed build-12340 Spell.dbc")
    parser.add_argument("--item-template", type=Path, required=True)
    parser.add_argument("--faction-spells", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("generated"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    spells = read_spells(args.spell_dbc)
    items = read_items(args.item_template)
    pairs = read_faction_pairs(args.faction_spells)
    catalog, sources, groups = build_catalog(spells, items, pairs)
    aura_candidates = {spell_id for spell_id, spell in spells.items() if MOUNTED_AURA in spell.auras}
    accepted_ids = {row["spell"] for row in catalog}
    unresolved = sorted(spell_id for spell_id in accepted_ids if not resolves_mount(spell_id, spells))
    digest = hashlib.sha256((args.spell_dbc.read_bytes() + args.item_template.read_bytes() +
                             args.faction_spells.read_bytes())).hexdigest()
    triggered = {target for spell in spells.values() for target in spell.triggers if target}
    rejected = aura_candidates - accepted_ids
    categories = {"test-or-debug-name": 0, "temporary-quest-rental-or-vehicle-name": 0,
                  "internal-trigger-spell": 0, "no-durable-player-learn-source": 0}
    for spell_id in rejected:
        name = spells[spell_id].name.lower()
        if re.search(r"\b(test|dnt|debug|video|copy)\b", name):
            categories["test-or-debug-name"] += 1
        elif re.search(r"\b(rental|quest|vehicle|taxi|passenger|racing)\b", name):
            categories["temporary-quest-rental-or-vehicle-name"] += 1
        elif spell_id in triggered:
            categories["internal-trigger-spell"] += 1
        else:
            categories["no-durable-player-learn-source"] += 1
    report = dict(sourceSha256=digest, candidateMountedAuraSpells=len(aura_candidates),
                  acceptedCollectibleSpells=len(catalog), logicalMounts=len({r["key"] for r in catalog}),
                  rejectedMountedAuraSpells=len(rejected), rejectionCategories=categories,
                  equivalentGroups=sorted([sorted(v) for v in groups.values() if len(v) > 1]),
                  manualIncludeOverrides=sorted(TRAINER_MOUNTS),
                  acceptedWithoutResolvedMountedAura=unresolved)
    canonical = json.dumps({"schema": 1, "statistics": report, "mounts": catalog},
                           indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    write_or_check(args.output_dir / "mount_catalog.json", canonical, args.check)
    write_or_check(args.output_dir / "NativeMountCatalog.inc", render_cpp(catalog, digest), args.check)
    write_or_check(Path("content/client/Interface/FrameXML/NativeMountCatalog.lua"),
                   render_lua(catalog, digest), args.check)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
