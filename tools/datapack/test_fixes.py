#!/usr/bin/env python3
"""Tests for fixes.py (manifest planning and idempotent application).

    python3 -I tools/datapack/test_fixes.py
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixes  # noqa: E402

MONSTER = """local mType = Game.createMonsterType("Sample Beast")
local monster = {}

monster.loot = {
	{ name = "gold coin", chance = 90000, maxCount = 40 },
	{ id = 9001, chance = 20000 }, -- beast fang
	{ name = "beast hide", chance = 500 },
}

mType:register(monster)
"""

BY_ID = {3031: "gold coin", 9001: "beast fang", 9002: "beast hide", 9003: "rare gem", 9005: "magic dust"}


def max_count(item, value):
    return {"kind": "max_count", "file": "beast.lua", "item": item, "maxCount": value}


def add(item, chance, count=None):
    fix = {"kind": "add_loot", "file": "beast.lua", "item": item, "chance": chance}
    if count:
        fix["maxCount"] = count
    return fix


class ApplyTest(unittest.TestCase):
    def test_max_count_is_raised_in_place(self):
        text, outcomes = fixes.apply_to_text(MONSTER, [max_count("gold coin", 60)], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn('{ name = "gold coin", chance = 90000, maxCount = 60 },', text)

    def test_max_count_is_added_to_an_id_entry_and_keeps_its_comment(self):
        text, _ = fixes.apply_to_text(MONSTER, [max_count("beast fang", 3)], BY_ID)
        self.assertIn("{ id = 9001, chance = 20000, maxCount = 3 }, -- beast fang", text)

    def test_max_count_never_lowers(self):
        text, outcomes = fixes.apply_to_text(MONSTER, [max_count("gold coin", 30)], BY_ID)
        self.assertEqual(["already"], outcomes)
        self.assertEqual(MONSTER, text)

    def test_new_item_is_inserted_in_chance_order(self):
        text, outcomes = fixes.apply_to_text(MONSTER, [add("rare gem", 1200), add("magic dust", 50, 4)], BY_ID)
        self.assertEqual(["applied", "applied"], outcomes)
        lines = [line.strip() for line in text.splitlines()]
        gem = lines.index('{ name = "rare gem", chance = 1200 },')
        self.assertEqual('{ name = "beast hide", chance = 500 },', lines[gem + 1])
        self.assertEqual('{ name = "magic dust", chance = 50, maxCount = 4 },', lines[lines.index('{ name = "beast hide", chance = 500 },') + 1])

    def test_applying_twice_changes_nothing(self):
        batch = [max_count("gold coin", 60), add("rare gem", 1200)]
        once, _ = fixes.apply_to_text(MONSTER, batch, BY_ID)
        twice, outcomes = fixes.apply_to_text(once, batch, BY_ID)
        self.assertEqual(once, twice)
        self.assertEqual(["already", "already"], outcomes)

    def test_item_added_by_upstream_is_left_alone(self):
        upstream = MONSTER.replace('{ name = "beast hide", chance = 500 },', '{ name = "beast hide", chance = 500 },\n\t{ id = 9003, chance = 900 },')
        text, outcomes = fixes.apply_to_text(upstream, [add("rare gem", 1200)], BY_ID)
        self.assertEqual(["already"], outcomes)
        self.assertEqual(upstream, text)

    def test_empty_loot_block_gets_the_entry(self):
        empty = 'local mType = Game.createMonsterType("X")\nlocal monster = {}\nmonster.loot = {}\nmType:register(monster)\n'
        text, outcomes = fixes.apply_to_text(empty, [add("rare gem", 1200)], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn('monster.loot = {\n\t{ name = "rare gem", chance = 1200 },\n}', text)

    def test_missing_loot_block_is_a_failure(self):
        _, outcomes = fixes.apply_to_text("local monster = {}\n", [add("rare gem", 1200)], BY_ID)
        self.assertEqual(["no monster.loot block"], outcomes)

    def test_manifest_application_writes_files_and_reports(self):
        with tempfile.TemporaryDirectory() as root:
            with open(os.path.join(root, "beast.lua"), "w", encoding="utf-8") as handle:
                handle.write(MONSTER)
            manifest = {"fixes": [max_count("gold coin", 60), add("rare gem", 1200), {**add("x", 5), "file": "gone.lua"}]}
            summary, failures = fixes.apply_manifest(manifest, root, BY_ID)
            self.assertEqual({"applied": 2, "file missing": 1}, summary)
            self.assertEqual(["gone.lua"], [f["file"] for f in failures])
            summary, _ = fixes.apply_manifest({"fixes": manifest["fixes"][:2]}, root, BY_ID)
            self.assertEqual({"already": 2}, summary)


class PlanTest(unittest.TestCase):
    REPORT = {"monsters": [{"name": "Sample Beast", "file": "beast.lua", "loot_revid": 7,
                            "loot_block": {"version": "13.0", "kills": 5000},
                            "findings": [
                                {"kind": "loot_count", "subject": "gold coin", "datapack_max": 40, "wiki_observed_max": 60},
                                {"kind": "loot_count", "subject": "beast hide", "datapack_max": 1, "wiki_observed_max": 2},
                                {"kind": "loot_count", "subject": "beast fang", "datapack_max": 3, "wiki_observed_max": 400},
                                {"kind": "loot_missing", "subject": "rare gem", "wiki_chance": 1200, "times": 60, "max_amount": 1},
                                {"kind": "loot_missing", "subject": "dragon trophy", "wiki_chance": 77, "times": 13, "max_amount": 1},
                                {"kind": "loot_chance", "subject": "gold coin", "datapack_chance": 1, "wiki_chance": 2},
                            ]}]}
    BY_NAME = {"gold coin": {3031}, "beast hide": {9002}, "beast fang": {9001}, "rare gem": {9003}, "dragon trophy": {2681, 2682}}

    def test_currency_and_old_statistics_are_not_added(self):
        report = {"monsters": [
            {"name": "Rich Beast", "file": "rich.lua", "loot_block": {"version": "14.1", "kills": 900},
             "findings": [{"kind": "loot_missing", "subject": "platinum coin", "wiki_chance": 50000, "times": 450, "max_amount": 3}]},
            {"name": "Old Beast", "file": "old.lua", "loot_block": {"version": "8.6", "kills": 90000},
             "findings": [{"kind": "loot_missing", "subject": "rare gem", "wiki_chance": 1200, "times": 1080, "max_amount": 1}]},
        ]}
        planned, review = fixes.plan(report, {"platinum coin": {3035}, "rare gem": {9003}}, stackable={3035})
        self.assertEqual([], planned)
        reasons = {r["item"]: r["reason"] for r in review}
        self.assertIn("currency", reasons["platinum coin"])
        self.assertIn("old statistics", reasons["rare gem"])

    def test_bosses_are_left_for_review(self):
        planned, review = fixes.plan(self.REPORT, self.BY_NAME, stackable={3031, 9001}, bosses={"Sample Beast"})
        self.assertEqual([], planned)
        self.assertTrue(all("reward chest" in r["reason"] for r in review))

    def test_only_safe_fixes_are_planned(self):
        planned, review = fixes.plan(self.REPORT, self.BY_NAME, stackable={3031, 9001})
        self.assertEqual([("add_loot", "rare gem"), ("max_count", "gold coin")], [(f["kind"], f["item"]) for f in planned])
        reasons = {r["item"]: r["reason"] for r in review}
        self.assertIn("not stackable", reasons["beast hide"])
        self.assertIn("too large", reasons["beast fang"])
        self.assertIn("2 items", reasons["dragon trophy"])
        self.assertEqual(7, planned[0]["source"]["loot_revid"])


class ChanceTest(unittest.TestCase):
    def test_chance_is_set_on_the_single_entry(self):
        text, outcomes = fixes.apply_to_text(MONSTER, [{"kind": "chance", "file": "beast.lua", "item": "beast fang", "chance": 15000}], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn("{ id = 9001, chance = 15000 }, -- beast fang", text)
        again, outcomes = fixes.apply_to_text(text, [{"kind": "chance", "file": "beast.lua", "item": "beast fang", "chance": 15000}], BY_ID)
        self.assertEqual(["already"], outcomes)
        self.assertEqual(text, again)

    def test_chance_with_several_entries_is_a_failure(self):
        doubled = MONSTER.replace('{ name = "beast hide", chance = 500 },', '{ name = "beast hide", chance = 500 },\n\t{ name = "beast hide", chance = 300 },')
        _, outcomes = fixes.apply_to_text(doubled, [{"kind": "chance", "file": "beast.lua", "item": "beast hide", "chance": 900}], BY_ID)
        self.assertEqual(["several entries"], outcomes)

    def test_chance_plan_rules(self):
        def finding(item, ours, wiki, entries=1):
            return {"kind": "loot_chance", "subject": item, "datapack_chance": ours, "wiki_chance": wiki, "times": 50, "kills": 9000, "entries": entries}
        report = {"monsters": [
            {"name": "New Beast", "file": "new.lua", "loot_block": {"version": "13.2", "kills": 9000}, "findings": [
                finding("beast fang", 20000, 15000), finding("gold coin", 50000, 90000), finding("beast hide", 500, 900, entries=2),
                finding("rare gem", 100, 9000)]},
            {"name": "Old Beast", "file": "old.lua", "loot_block": {"version": "8.6", "kills": 90000}, "findings": [finding("beast fang", 20000, 15000)]},
        ]}
        planned, review = fixes.plan(report, {}, stackable=set(), kinds=("chance",))
        self.assertEqual([("New Beast", "beast fang", 15000)], [(f["monster"], f["item"], f["chance"]) for f in planned])
        reasons = {(r["monster"], r["item"]): r["reason"] for r in review}
        self.assertIn("currency", reasons[("New Beast", "gold coin")])
        self.assertIn("entries", reasons[("New Beast", "beast hide")])
        self.assertIn("too large", reasons[("New Beast", "rare gem")])
        self.assertIn("old statistics", reasons[("Old Beast", "beast fang")])


ATTRIBUTES = """local monster = {}
monster.experience = 700
monster.health = 1000
monster.maxHealth = 1000
monster.defenses = {
	defense = 30,
	armor = 25,
	mitigation = 0.99,
	{ name = "combat", interval = 2000, chance = 15, minDamage = 40, maxDamage = 70 },
}
monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 100 },
	{ type = COMBAT_ICEDAMAGE, percent = -10 },
}
monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "invisible", condition = false },
}
"""


class AttributeTest(unittest.TestCase):
    def test_stats_elements_and_immunities_are_set(self):
        batch = [
            {"kind": "stat", "item": "experience", "value": 750},
            {"kind": "stat", "item": "maxHealth", "value": 1100},
            {"kind": "stat", "item": "armor", "value": 30},
            {"kind": "element", "item": "COMBAT_FIREDAMAGE", "percent": 80},
            {"kind": "element", "item": "COMBAT_ICEDAMAGE", "percent": -15},
            {"kind": "immunity", "item": "invisible", "condition": True},
        ]
        text, outcomes = fixes.apply_to_text(ATTRIBUTES, batch, BY_ID)
        self.assertEqual(["applied"] * 6, outcomes)
        for line in ("monster.experience = 750", "monster.health = 1100", "monster.maxHealth = 1100", "\tarmor = 30,",
                     "{ type = COMBAT_FIREDAMAGE, percent = 80 }", "{ type = COMBAT_ICEDAMAGE, percent = -15 }",
                     '{ type = "invisible", condition = true }', "mitigation = 0.99"):
            self.assertIn(line, text)
        again, outcomes = fixes.apply_to_text(text, batch, BY_ID)
        self.assertEqual(text, again)
        self.assertEqual(["already"] * 6, outcomes)

    def test_attribute_plan_needs_the_same_creature(self):
        def entry(name, race, wiki_race, findings):
            return {"name": name, "file": name + ".lua", "race_id": race, "wiki_race_id": wiki_race, "revid": 3, "findings": findings}
        report = {"monsters": [
            entry("Same", 34, 34.0, [{"kind": "stat", "subject": "experience", "datapack": 700, "wiki": 750.0},
                                    {"kind": "stat", "subject": "maxHealth", "datapack": 1000, "wiki": 5000.0},
                                    {"kind": "element", "subject": "fireDmgMod", "datapack": 100, "wiki": 80.0},
                                    {"kind": "immunity", "subject": "paralyze", "datapack": False, "wiki": True}]),
            entry("Variant", None, 34.0, [{"kind": "stat", "subject": "experience", "datapack": 700, "wiki": 750.0}]),
        ]}
        planned, review = fixes.plan(report, {}, set(), kinds=("stat", "element", "immunity"), element_types={"fireDmgMod": "COMBAT_FIREDAMAGE"})
        self.assertEqual({("stat", "experience", 750), ("element", "COMBAT_FIREDAMAGE", 80), ("immunity", "paralyze", True)},
                         {(f["kind"], f["item"], f.get("value", f.get("percent", f.get("condition")))) for f in planned})
        reasons = {(r["monster"], r["item"]): r["reason"] for r in review}
        self.assertIn("too large", reasons[("Same", "maxHealth")])
        self.assertIn("race id", reasons[("Variant", "experience")])


class MergeTest(unittest.TestCase):
    def test_earlier_fixes_are_kept_and_same_key_is_replaced(self):
        earlier = [max_count("gold coin", 60), add("rare gem", 1200)]
        later = [add("rare gem", 1500), add("magic dust", 50)]
        merged = fixes.merge_fixes(earlier, later)
        self.assertEqual(3, len(merged))
        self.assertEqual(1500, next(f for f in merged if f["item"] == "rare gem")["chance"])
        self.assertIn(("max_count", "gold coin"), [(f["kind"], f["item"]) for f in merged])


class DropTest(unittest.TestCase):
    def test_fixes_of_excluded_monsters_leave_the_manifest(self):
        kept, dropped = fixes.drop_excluded([{**add("rare gem", 10), "monster": "Boss"}, {**add("x", 5), "monster": "Beast"}], {"Boss"})
        self.assertEqual(["Beast"], [f["monster"] for f in kept])
        self.assertEqual(["Boss"], [f["monster"] for f in dropped])


class StackableTest(unittest.TestCase):
    def test_cumulative_flag_is_read_from_the_protobuf(self):
        def varint(n):
            out = bytearray()
            while True:
                byte = n & 0x7F
                n >>= 7
                out.append(byte | (0x80 if n else 0))
                if not n:
                    return bytes(out)

        def field(number, payload):
            return varint(number << 3 | 2) + varint(len(payload)) + payload

        flags = varint(6 << 3) + varint(1)
        stack = field(1, varint(1 << 3) + varint(3031) + field(3, flags))
        single = field(1, varint(1 << 3) + varint(3280) + field(3, b""))
        with tempfile.NamedTemporaryFile(delete=False) as handle:
            handle.write(stack + single)
        try:
            self.assertEqual({3031}, fixes.load_stackable(handle.name))
        finally:
            os.unlink(handle.name)


if __name__ == "__main__":
    unittest.main()
