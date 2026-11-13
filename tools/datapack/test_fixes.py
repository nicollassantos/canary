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
    REPORT = {"monsters": [{"name": "Sample Beast", "file": "beast.lua", "loot_revid": 7, "race_id": 900, "wiki_race_id": 900.0,
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
            {"name": "Rich Beast", "file": "rich.lua", "race_id": 1, "loot_block": {"version": "14.1", "kills": 900},
             "findings": [{"kind": "loot_missing", "subject": "platinum coin", "wiki_chance": 50000, "times": 450, "max_amount": 3}]},
            {"name": "Old Beast", "file": "old.lua", "race_id": 2, "loot_block": {"version": "8.6", "kills": 90000},
             "findings": [{"kind": "loot_missing", "subject": "rare gem", "wiki_chance": 1200, "times": 1080, "max_amount": 1}]},
        ]}
        planned, review = fixes.plan(report, {"platinum coin": {3035}, "rare gem": {9003}}, stackable={3035})
        self.assertEqual([], planned)
        reasons = {r["item"]: r["reason"] for r in review}
        self.assertIn("currency", reasons["platinum coin"])
        self.assertIn("old statistics", reasons["rare gem"])

    def test_loot_of_a_creature_without_race_id_is_left_for_review(self):
        report = {"monsters": [{**self.REPORT["monsters"][0], "race_id": None}]}
        planned, review = fixes.plan(report, self.BY_NAME, stackable={3031, 9001})
        self.assertEqual([], planned)
        self.assertTrue(all("identity" in r["reason"] for r in review))

    def test_planned_fixes_record_the_value_they_replace(self):
        planned, _ = fixes.plan(self.REPORT, self.BY_NAME, stackable={3031, 9001})
        self.assertEqual(40, next(f for f in planned if f["kind"] == "max_count")["from"])

    def test_bosses_are_left_for_review(self):
        planned, review = fixes.plan(self.REPORT, self.BY_NAME, stackable={3031, 9001}, bosses={"Sample Beast"})
        self.assertEqual([], planned)
        self.assertTrue(all("reward chest" in r["reason"] for r in review))

    def test_reward_boss_attributes_follow_the_normal_rules(self):
        report = {"monsters": [{"name": "Chest Boss", "file": "boss.lua", "race_id": 7, "wiki_race_id": 7.0, "revid": 1, "findings": [
            {"kind": "stat", "subject": "armor", "datapack": 40, "wiki": 50.0},
            {"kind": "loot_count", "subject": "gold coin", "datapack_max": 40, "wiki_observed_max": 60}]}]}
        planned, review = fixes.plan(report, {"gold coin": {3031}}, stackable={3031}, bosses={"Chest Boss"})
        self.assertEqual([("stat", "armor")], [(f["kind"], f["item"]) for f in planned])
        self.assertEqual(["gold coin"], [r["item"] for r in review])


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
            {"name": "New Beast", "file": "new.lua", "race_id": 3, "loot_block": {"version": "13.2", "kills": 9000}, "findings": [
                finding("beast fang", 20000, 15000), finding("gold coin", 50000, 90000), finding("beast hide", 500, 900, entries=2),
                {**finding("rare gem", 100, 9000), "times": 10}]},  # small single sample: not trusted
            {"name": "Old Beast", "file": "old.lua", "race_id": 4, "loot_block": {"version": "8.6", "kills": 90000}, "findings": [finding("beast fang", 20000, 15000)]},
        ]}
        planned, review = fixes.plan(report, {}, stackable=set(), kinds=("chance",))
        # The wiki is the source of truth: the large rare gem gap is applied too.
        self.assertEqual([("New Beast", "beast fang", 15000), ("New Beast", "rare gem", 9000)], [(f["monster"], f["item"], f["chance"]) for f in planned])
        reasons = {(r["monster"], r["item"]): r["reason"] for r in review}
        self.assertIn("currency", reasons[("New Beast", "gold coin")])
        self.assertIn("entries", reasons[("New Beast", "beast hide")])
        self.assertNotIn(("New Beast", "rare gem"), reasons)  # large gap applied: the wiki is the source of truth
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
        # The wiki is the source of truth: a large stat gap is applied when the creature is the same.
        self.assertEqual({("stat", "experience", 750), ("stat", "maxHealth", 5000), ("element", "COMBAT_FIREDAMAGE", 80), ("immunity", "paralyze", True)},
                         {(f["kind"], f["item"], f.get("value", f.get("percent", f.get("condition")))) for f in planned})
        reasons = {(r["monster"], r["item"]): r["reason"] for r in review}
        self.assertIn("race id", reasons[("Variant", "experience")])


class GapEvidenceTest(unittest.TestCase):
    def finding(self, ours, wiki, times, kills, others=()):
        return {"datapack_chance": ours, "wiki_chance": wiki, "times": times, "kills": kills, "other_versions": list(others)}

    def test_another_version_confirms_the_wiki_rate(self):
        self.assertEqual("confirmed by another version", fixes.gap_evidence(self.finding(500, 67066, 112, 167, [["12.31", 3834, 2365]])))

    def test_a_version_that_agrees_with_the_datapack_blocks_the_fix(self):
        self.assertIsNone(fixes.gap_evidence(self.finding(5090, 922, 380, 41224, [["15.23", 13095, 395], ["14.12", 1151, 60]])))

    def test_single_block_needs_a_large_sample(self):
        self.assertEqual("single large sample", fixes.gap_evidence(self.finding(250, 13492, 1580, 11711)))
        self.assertIsNone(fixes.gap_evidence(self.finding(18920, 149, 1, 669)))

    def test_versions_that_disagree_with_both_block_the_fix(self):
        self.assertIsNone(fixes.gap_evidence(self.finding(1080, 25, 1, 4053, [["8.6", 19837, 0]])))


class OldStatisticsTest(unittest.TestCase):
    def finding(self, kind, item, wiki, rarity, ours=None):
        base = {"kind": kind, "subject": item, "wiki_chance": wiki, "times": 40, "kills": 20000, "current_rarity": rarity, "max_amount": 1}
        if kind == "loot_chance":
            base.update({"datapack_chance": ours, "entries": 1, "other_versions": []})
        return base

    def test_old_rate_inside_the_current_rarity_is_applied_and_evidence_does_not_leak(self):
        report = {"monsters": [{"name": "Old Beast", "file": "old.lua", "race_id": 2, "loot_block": {"version": "8.6", "kills": 20000}, "findings": [
            self.finding("loot_chance", "beast fang", 3000, "semi-rare", ours=20000),   # wiki 3% fits semi-rare, datapack 20% does not
            self.finding("loot_chance", "beast hide", 3000, "uncommon", ours=6000),     # datapack 6% already fits uncommon
            self.finding("loot_missing", "rare gem", 700, "rare"),                     # 0.7% fits rare
            self.finding("loot_missing", "magic dust", 700, "very rare"),              # 0.7% does not fit very rare
        ]}]}
        planned, review = fixes.plan(report, {"rare gem": {9003}, "magic dust": {9005}}, set())
        self.assertEqual({("chance", "beast fang"), ("add_loot", "rare gem")}, {(f["kind"], f["item"]) for f in planned})
        self.assertTrue(all("rarity" in f["source"]["evidence"] for f in planned))
        reasons = {r["item"]: r["reason"] for r in review}
        self.assertIn("already fits", reasons["beast hide"])
        self.assertIn("old statistics", reasons["magic dust"])

    def test_gap_evidence_stays_on_its_own_fix(self):
        report = {"monsters": [{"name": "New Beast", "file": "new.lua", "race_id": 3, "loot_block": {"version": "13.2", "kills": 20000}, "findings": [
            {"kind": "loot_chance", "subject": "beast fang", "datapack_chance": 100, "wiki_chance": 9000, "times": 1800, "kills": 20000, "entries": 1, "other_versions": []},
            {"kind": "loot_chance", "subject": "beast hide", "datapack_chance": 2000, "wiki_chance": 3000, "times": 600, "kills": 20000, "entries": 1, "other_versions": []},
        ]}]}
        planned, _ = fixes.plan(report, {}, set())
        sources = {f["item"]: f["source"] for f in planned}
        self.assertEqual("single large sample", sources["beast fang"]["evidence"])
        self.assertNotIn("evidence", sources["beast hide"])


class ReviewedTest(unittest.TestCase):
    def test_keep_decision_sends_the_finding_to_review(self):
        report = {"monsters": [{"name": "Shock Head", "file": "shock.lua", "race_id": 5, "wiki_race_id": 5.0, "revid": 1,
                                "findings": [{"kind": "stat", "subject": "armor", "datapack": 30, "wiki": 78.0}]}]}
        planned, _ = fixes.plan(report, {}, set(), kinds=("stat",))
        self.assertEqual([(78, 30)], [(f["value"], f["from"]) for f in planned])
        decisions = [{"monster": "Shock Head", "kind": "stat", "item": "armor", "decision": "keep", "note": "tuned for the quest"}]
        planned, review = fixes.plan(report, {}, set(), kinds=("stat",), reviewed=decisions)
        self.assertEqual([], planned)
        self.assertIn("tuned for the quest", review[0]["reason"])

    def test_hit_points_without_a_max_health_line(self):
        only_health = ATTRIBUTES.replace("monster.maxHealth = 1000\n", "")
        text, outcomes = fixes.apply_to_text(only_health, [{"kind": "stat", "item": "maxHealth", "value": 1200, "from": 1000}], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn("monster.health = 1200", text)

    def test_mitigation_is_set_with_two_decimals(self):
        fix = {"kind": "stat", "item": "mitigation", "value": 1.56, "from": 0.99}
        text, outcomes = fixes.apply_to_text(ATTRIBUTES, [fix], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn("mitigation = 1.56,", text)
        self.assertEqual(["already"], fixes.apply_to_text(text, [fix], BY_ID)[1])


class ResolveTest(unittest.TestCase):
    BY_NAME = {"energy ring": {3051, 3088}, "ice flower": {9500}, "giant shimmering pearl": {281, 282}, "dragon trophy": {2681, 2682}}
    FLAGS = {3051: {"take", "market"}, 3088: {"take"}, 9500: {"take", "market"}, 281: {"take", "market"}, 282: {"take", "market"}}

    def resolve(self, name):
        return fixes.resolve_item(name, self.BY_NAME, self.FLAGS)

    def test_ambiguous_name_resolves_to_the_only_tradeable_item(self):
        self.assertEqual((3051, "flags"), self.resolve("energy ring"))  # 3088 is the worn ring

    def test_wiki_suffix_is_dropped_when_one_candidate_remains(self):
        self.assertEqual((9500, "flags"), self.resolve("ice flower (item)"))

    def test_colour_suffix_with_several_candidates_stays_unresolved(self):
        item_id, reason = self.resolve("giant shimmering pearl (green)")
        self.assertIsNone(item_id)
        self.assertIn("2 items", reason)

    def test_decoration_is_never_picked(self):
        self.assertIsNone(self.resolve("dragon trophy")[0])

    def test_added_by_id_writes_the_id_and_is_idempotent(self):
        fix = {"kind": "add_loot", "file": "beast.lua", "item": "energy ring", "id": 3051, "chance": 700, "by_id": True}
        text, outcomes = fixes.apply_to_text(MONSTER, [fix], {**BY_ID, 3051: "energy ring"})
        self.assertEqual(["applied"], outcomes)
        self.assertIn("{ id = 3051, chance = 700 }, -- energy ring", text)
        again, outcomes = fixes.apply_to_text(text, [fix], {**BY_ID, 3051: "energy ring"})
        self.assertEqual((text, ["already"]), (again, outcomes))


COIN_MONSTER = """monster.loot = {
	{ name = "gold coin", chance = 50000, maxCount = 205 },
	{ name = "gold coin", chance = 50000, maxCount = 100 },
	{ id = 9001, chance = 20000 }, -- beast fang
	{ name = "gold coin", chance = 40000, maxCount = 35 },
}
"""


class CoinsTest(unittest.TestCase):
    FIX = {"kind": "coins", "file": "beast.lua", "item": "money",
           "entries": [["gold coin", 99000, 1, 120], ["platinum coin", 76667, 1, 3]],
           "from": [["gold coin", 50000, 1, 205], ["gold coin", 50000, 1, 100], ["gold coin", 40000, 1, 35]]}

    def test_coin_entries_are_replaced_in_place_and_idempotent(self):
        text, outcomes = fixes.apply_to_text(COIN_MONSTER, [self.FIX], {**BY_ID, 3031: "gold coin"})
        self.assertEqual(["applied"], outcomes)
        lines = [l.strip() for l in text.splitlines()]
        self.assertEqual(['{ name = "gold coin", chance = 99000, maxCount = 120 },', '{ name = "platinum coin", chance = 76667, maxCount = 3 },',
                          "{ id = 9001, chance = 20000 }, -- beast fang"], lines[1:4])
        self.assertEqual(1, text.count("gold coin"))
        again, outcomes = fixes.apply_to_text(text, [self.FIX], BY_ID)
        self.assertEqual((text, ["already"]), (again, outcomes))

    def test_coins_changed_upstream_are_reported(self):
        upstream = COIN_MONSTER.replace("maxCount = 205", "maxCount = 250")
        text, outcomes = fixes.apply_to_text(upstream, [self.FIX], BY_ID)
        self.assertEqual(upstream, text)
        self.assertTrue(outcomes[0].startswith("changed upstream"))


class SupersedeTest(unittest.TestCase):
    def test_coins_fix_supersedes_other_coin_fixes_of_the_monster(self):
        batch = [{**max_count("gold coin", 104), "monster": "Dragon"}, {**max_count("dragon ham", 3), "monster": "Dragon"},
                 {"kind": "coins", "file": "beast.lua", "monster": "Dragon", "item": "money", "entries": [], "from": []},
                 {**max_count("gold coin", 60), "monster": "Wolf"}]
        batch[0]["from"] = 102
        batch[2]["from"] = [["gold coin", 89920, 1, 104]]
        kept, dropped = fixes.supersede_coins(batch, previous_keys={("beast.lua", "max_count", "gold coin")})
        self.assertEqual([("Dragon", "gold coin")], [(f["monster"], f["item"]) for f in dropped])
        self.assertEqual(3, len(kept))
        # The coins fix now expects upstream's line, not the raised maxCount.
        self.assertEqual([["gold coin", 89920, 1, 102]], next(f for f in kept if f["kind"] == "coins")["from"])


class MergeTest(unittest.TestCase):
    def test_replanned_fix_keeps_the_first_from(self):
        earlier = [{**max_count("gold coin", 60), "from": 40}]
        later = [{**max_count("gold coin", 70), "from": 60}]  # planned again after apply: reads our 60
        self.assertEqual([(70, 40)], [(f["maxCount"], f["from"]) for f in fixes.merge_fixes(earlier, later)])

    def test_coins_fix_already_in_the_manifest_keeps_its_from(self):
        coins = {"kind": "coins", "file": "beast.lua", "monster": "Dragon", "item": "money", "entries": [], "from": [["gold coin", 1, 1, 100]]}
        raise_now = {**max_count("gold coin", 100), "monster": "Dragon", "from": 99}
        kept, _ = fixes.supersede_coins([coins, raise_now], previous_keys={("beast.lua", "coins", "money")})
        self.assertEqual([["gold coin", 1, 1, 100]], kept[0]["from"])

    def test_earlier_fixes_are_kept_and_same_key_is_replaced(self):
        earlier = [max_count("gold coin", 60), add("rare gem", 1200)]
        later = [add("rare gem", 1500), add("magic dust", 50)]
        merged = fixes.merge_fixes(earlier, later)
        self.assertEqual(3, len(merged))
        self.assertEqual(1500, next(f for f in merged if f["item"] == "rare gem")["chance"])
        self.assertIn(("max_count", "gold coin"), [(f["kind"], f["item"]) for f in merged])


class ValidateTest(unittest.TestCase):
    ELEMENT_KEYS = {"COMBAT_DROWNDAMAGE": "drownDmgMod"}

    def check(self, fix, **info):
        return fixes.validate(fix, info, self.ELEMENT_KEYS)

    def test_loot_needs_a_datapack_race_id_that_matches_when_the_wiki_has_one(self):
        self.assertIsNone(self.check(add("rare gem", 10), race=34, wiki_race=34))
        self.assertIsNone(self.check(add("rare gem", 10), race=34, wiki_race=None))
        self.assertIn("no race id", self.check(add("rare gem", 10), race=None, wiki_race=34))
        self.assertIn("differs", self.check(add("rare gem", 10), race=35, wiki_race=34))

    def test_reward_chest_monsters_keep_their_loot_but_not_their_attributes_out(self):
        self.assertIn("reward chest", self.check(add("rare gem", 10), race=34, wiki_race=34, reward=True))
        element = {"kind": "element", "item": "COMBAT_DROWNDAMAGE", "percent": 0}
        self.assertIsNone(self.check(element, race=34, wiki_race=34, reward=True, uncertain=set()))

    def test_attributes_need_matching_ids_and_a_certain_value(self):
        element = {"kind": "element", "item": "COMBAT_DROWNDAMAGE", "percent": 0}
        self.assertIsNone(self.check(element, race=34, wiki_race=34, uncertain=set()))
        self.assertIn("uncertain", self.check(element, race=34, wiki_race=34, uncertain={"drownDmgMod"}))
        self.assertIn("race id", self.check(element, race=34, wiki_race=None))


class ConflictTest(unittest.TestCase):
    def test_value_changed_upstream_is_reported_not_overwritten(self):
        batch = [{**max_count("gold coin", 60), "from": 30},
                 {"kind": "chance", "file": "beast.lua", "item": "beast hide", "chance": 900, "from": 700}]
        text, outcomes = fixes.apply_to_text(MONSTER, batch, BY_ID)
        self.assertEqual(MONSTER, text)
        self.assertTrue(all(o.startswith("changed upstream") for o in outcomes))

    def test_attribute_changed_upstream_is_reported(self):
        fix = {"kind": "stat", "item": "experience", "value": 750, "from": 650}
        text, outcomes = fixes.apply_to_text(ATTRIBUTES, [fix], BY_ID)
        self.assertEqual(ATTRIBUTES, text)
        self.assertTrue(outcomes[0].startswith("changed upstream"))

    def test_value_still_at_from_is_changed(self):
        fix = {"kind": "stat", "item": "experience", "value": 750, "from": 700}
        text, outcomes = fixes.apply_to_text(ATTRIBUTES, [fix], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn("monster.experience = 750", text)


def removal(item, entries):
    return {"kind": "remove_loot", "file": "beast.lua", "item": item, "from": entries}


def absent(item, **overrides):
    finding = {"kind": "loot_datapack_only", "subject": item, "datapack_chance": 500, "kills": 10000,
               "datapack_entries": [[500, 1, 1]], "current_listed": False, "current_size": 12, "unresolved_wiki_names": []}
    finding.update(overrides)
    return finding


class RemoveLootTest(unittest.TestCase):
    def plan(self, findings, version="14.0", **monster):
        report = {"monsters": [{"name": "Sample Beast", "file": "beast.lua", "race_id": 900, "wiki_race_id": 900.0,
                                "loot_block": {"version": version, "kills": 10000}, "findings": findings, **monster}]}
        return fixes.plan(report, {"beast hide": {9002}}, stackable=set(), kinds=("remove_loot",))

    def test_absent_item_is_removed_with_its_upstream_entries_and_evidence(self):
        planned, review = self.plan([absent("beast hide")])
        self.assertEqual([], review)
        self.assertEqual([[500, 1, 1]], planned[0]["from"])
        self.assertIn("no drop in 10000 kills (about 50 expected)", planned[0]["source"]["evidence"])

    def test_removal_gates(self):
        cases = {
            "gold coin": absent("gold coin"),
            "#44736": absent("#44736"),
            "listed": absent("listed", current_listed=True),
            "renamed": absent("renamed", unresolved_wiki_names=["mystery lump"]),
        }
        planned, review = self.plan(list(cases.values()))
        self.assertEqual([], planned)
        reasons = {r["item"]: r["reason"] for r in review}
        self.assertIn("coins", reasons["gold coin"])
        self.assertIn("does not name", reasons["#44736"])
        self.assertIn("current loot table lists", reasons["listed"])
        self.assertIn("mystery lump", reasons["renamed"])

    def test_old_statistics_need_a_current_loot_table_that_lacks_the_item(self):
        planned, _ = self.plan([absent("beast hide")], version="8.6")
        self.assertIn("current loot table lacks it", planned[0]["source"]["evidence"])
        planned, review = self.plan([absent("beast hide", current_size=0)], version="8.6")
        self.assertEqual([], planned)
        self.assertIn("no current loot table", review[0]["reason"])

    def test_reward_chest_and_unconfirmed_identity_stay_out(self):
        report = {"monsters": [{"name": "Sample Beast", "file": "beast.lua", "race_id": None, "loot_block": {"version": "14.0", "kills": 10000},
                                "findings": [absent("beast hide")]}]}
        self.assertEqual([], fixes.plan(report, {}, set(), kinds=("remove_loot",))[0])
        self.assertEqual([], fixes.plan({"monsters": [{**report["monsters"][0], "race_id": 900}]}, {}, set(), kinds=("remove_loot",),
                                        bosses={"Sample Beast"})[0])
        self.assertIn("reward chest", fixes.validate(removal("beast hide", []), {"race": 900, "reward": True}, {}))

    def test_entries_are_removed_once_and_comments_go_with_them(self):
        fix = removal("beast fang", [[20000, 1, 1]])
        text, outcomes = fixes.apply_to_text(MONSTER, [fix], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertNotIn("9001", text)
        self.assertIn('{ name = "beast hide", chance = 500 },', text)
        again, outcomes = fixes.apply_to_text(text, [fix], BY_ID)
        self.assertEqual((text, ["already"]), (again, outcomes))

    def test_entry_changed_upstream_is_kept(self):
        text, outcomes = fixes.apply_to_text(MONSTER, [removal("beast hide", [[700, 1, 1]])], BY_ID)
        self.assertEqual(MONSTER, text)
        self.assertTrue(outcomes[0].startswith("changed upstream"))

    def test_entry_on_several_lines_is_left_for_a_person(self):
        nested = MONSTER.replace('\t{ name = "beast hide", chance = 500 },', '\t{\n\t\tname = "beast hide",\n\t\tchance = 500,\n\t},')
        text, outcomes = fixes.apply_to_text(nested, [removal("beast hide", [[500, 1, 1]])], BY_ID)
        self.assertEqual(nested, text)
        self.assertEqual(["changed upstream (entry not on one line)"], outcomes)


class DuplicateAddTest(unittest.TestCase):
    BY_NAME = {"book": {2828, 401}, "gore horn": {500}}

    def test_item_listed_under_another_spelling_is_not_added(self):
        text = MONSTER.replace('\t{ name = "beast hide", chance = 500 },', '\t{ id = 2828, chance = 730 }, -- book')
        fix = {"kind": "add_loot", "file": "beast.lua", "item": "book (orange)", "id": 401, "chance": 732, "by_id": True}
        again, outcomes = fixes.apply_to_text(text, [fix], {**BY_ID, 2828: "book", 401: "book"})
        self.assertEqual(text, again)
        self.assertTrue(outcomes[0].startswith("listed as another entry"))

    def test_validate_drops_an_add_the_datapack_already_holds(self):
        fix = {"kind": "add_loot", "item": "gore horn (item)", "id": 500, "chance": 41176, "by_id": True}
        doubled = [{"id": 500, "chance": 41176}, {"name": "Gore Horn", "chance": 36040}]
        reason = fixes.validate(fix, {"race": 1, "loot": doubled}, {}, self.BY_NAME)
        self.assertIn("already lists this item as Gore Horn", reason)
        # The fix's own line alone is not a duplicate.
        self.assertIsNone(fixes.validate(fix, {"race": 1, "loot": doubled[:1]}, {}, self.BY_NAME))

    def test_plan_leaves_an_aliased_missing_item_for_review(self):
        report = {"monsters": [{"name": "Sample Beast", "file": "beast.lua", "race_id": 900, "loot_block": {"version": "14.0", "kills": 10000},
                                "findings": [{"kind": "loot_missing", "subject": "pearl (green)", "wiki_chance": 3000, "times": 300,
                                              "max_amount": 1, "datapack_alias": ["pearl"]}]}]}
        planned, review = fixes.plan(report, {"pearl": {281, 282}}, set(), kinds=("add_loot",))
        self.assertEqual([], planned)
        self.assertIn("lists this item as pearl", review[0]["reason"])

    def test_add_and_removal_of_the_same_item_drop_out_together(self):
        batch = [{"kind": "add_loot", "file": "a.lua", "item": "book (orange)", "id": 401, "chance": 700},
                 removal("book", [[730, 1, 1]]) | {"file": "a.lua"},
                 {"kind": "chance", "file": "a.lua", "item": "beast hide", "chance": 900}]
        kept, dropped = fixes.loot_conflicts(batch, self.BY_NAME)
        self.assertEqual(["chance"], [f["kind"] for f in kept])
        self.assertEqual(2, len(dropped))


class LowerMaxCountTest(unittest.TestCase):
    def plan(self, version="14.0", **overrides):
        finding = {"kind": "loot_count_high", "subject": "beast fang", "datapack_max": 5, "datapack_min": 1, "wiki_observed_max": 3, "times": 2000}
        finding.update(overrides)
        report = {"monsters": [{"name": "Sample Beast", "file": "beast.lua", "race_id": 900, "loot_block": {"version": version, "kills": 10000},
                                "findings": [finding]}]}
        return fixes.plan(report, {"beast fang": {9001}}, stackable={9001}, kinds=("max_count",))

    def test_lowering_is_planned_from_recent_statistics(self):
        planned, review = self.plan()
        self.assertEqual([], review)
        self.assertEqual((3, 5), (planned[0]["maxCount"], planned[0]["from"]))
        self.assertIn("no drop above 3 in 2000 drops", planned[0]["source"]["evidence"])

    def test_old_statistics_and_counts_below_min_stay_for_review(self):
        self.assertIn("amounts may have changed", self.plan(version="8.6")[1][0]["reason"])
        self.assertIn("below the datapack minCount", self.plan(datapack_min=4)[1][0]["reason"])

    def test_lowering_is_exact_and_idempotent(self):
        text = MONSTER.replace("{ id = 9001, chance = 20000 }, -- beast fang", "{ id = 9001, chance = 20000, maxCount = 5 }, -- beast fang")
        fix = {**max_count("beast fang", 3), "from": 5}
        lowered, outcomes = fixes.apply_to_text(text, [fix], BY_ID)
        self.assertEqual(["applied"], outcomes)
        self.assertIn("{ id = 9001, chance = 20000, maxCount = 3 }, -- beast fang", lowered)
        self.assertEqual((lowered, ["already"]), fixes.apply_to_text(lowered, [fix], BY_ID))
        changed = text.replace("maxCount = 5", "maxCount = 4")
        self.assertEqual("changed upstream (maxCount 4)", fixes.apply_to_text(changed, [fix], BY_ID)[1][0])

    def test_lowering_needs_a_single_entry(self):
        doubled = MONSTER.replace('\t{ name = "beast hide", chance = 500 },', '\t{ id = 9001, chance = 100, maxCount = 5 },')
        self.assertEqual(["several entries"], fixes.apply_to_text(doubled, [{**max_count("beast fang", 3), "from": 5}], BY_ID)[1])


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
