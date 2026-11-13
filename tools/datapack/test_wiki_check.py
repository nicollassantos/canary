#!/usr/bin/env python3
"""Tests for wiki_check.py with synthetic fixtures in the wiki's format (no wiki text).

    python3 -I tools/datapack/test_wiki_check.py
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_check  # noqa: E402

LOOT_STATISTICS = """{{Loot2
|version=9.1
|kills=200
|name=Sample Beast
|Gold Coin, times:190, amount:1-40, total:4000
}}

{{Loot2
|version=12.40
|kills=10000
|name=Sample Beast
|Empty, times:100
|Gold Coin, times:9000, amount:1-60, total:270000
|Beast Fang, times:2000, amount:1-3, total:4000
|Beast Hide, times:500
|Rare Gem, times:12, amount:1, total:12
|Odd, Comma Item, times:30, amount:1, total:30
}}

{{Loot2
|version=8.6
|kills=50000
|name=Sample Beast
|Gold Coin, times:45000, amount:1-50, total:1000000
}}
"""

INFOBOX = """{{Infobox Creature|List={{{1|}}}
| name           = Sample Beast
| hp             = 1000
| exp            = 700
| armor          = 25
| mitigation     = 1.10
| paraimmune     = yes
| senseinvis     = no
| physicalDmgMod = 100%
| fireDmgMod     = 0%
| iceDmgMod      = 110%
| race_id        = 900
}}"""


def monster(**overrides):
    base = {
        "name": "Sample Beast", "file": "data-otservbr-global/monster/sample_beast.lua",
        "maxHealth": 1000, "experience": 700, "armor": 25, "mitigation": 1.10, "raceId": 900,
        "elements": [
            {"type": "COMBAT_PHYSICALDAMAGE", "percent": 0},
            {"type": "COMBAT_FIREDAMAGE", "percent": 100},
            {"type": "COMBAT_ICEDAMAGE", "percent": -10},
        ],
        "immunities": [{"type": "paralyze", "condition": True}, {"type": "invisible", "condition": False}],
        "loot": [
            {"name": "gold coin", "chance": 90000, "maxCount": 60},
            {"name": "beast fang", "chance": 20000, "maxCount": 3},
            {"name": "beast hide", "chance": 5000},
            {"name": "odd, comma item", "chance": 300},
        ],
    }
    base.update(overrides)
    return base


BY_NAME = {"gold coin": {3031}, "beast fang": {9001}, "beast hide": {9002}, "rare gem": {9003}, "odd, comma item": {9004}}
BY_ID = {3031: "gold coin", 9001: "beast fang", 9002: "beast hide", 9003: "rare gem", 9004: "odd, comma item"}


def kinds(findings):
    return {(f["kind"], f["subject"]) for f in findings}


class ParsingTest(unittest.TestCase):
    def test_blocks_are_parsed_and_empty_is_skipped(self):
        blocks = wiki_check.parse_loot_statistics(LOOT_STATISTICS)
        self.assertEqual(3, len(blocks))
        newest = next(b for b in blocks if b["version"] == "12.40")
        self.assertEqual(10000, newest["kills"])
        self.assertNotIn("empty", newest["items"])
        self.assertEqual((9000, 1, 60), newest["items"]["gold coin"])
        self.assertEqual((500, 1, 1), newest["items"]["beast hide"])  # row without amount
        self.assertIn("odd, comma item", newest["items"])

    def test_versions_compare_numerically(self):
        blocks = wiki_check.parse_loot_statistics(LOOT_STATISTICS)
        block, strong = wiki_check.choose_block(blocks)
        self.assertEqual("12.40", block["version"])  # newer than 9.1 and 8.6, not string order
        self.assertTrue(strong)

    def test_weak_block_when_no_version_has_enough_kills(self):
        block, strong = wiki_check.choose_block([{"version": "13.1", "kills": 40, "items": {}}, {"version": "12.0", "kills": 90, "items": {}}])
        self.assertEqual(90, block["kills"])
        self.assertFalse(strong)

    def test_infobox_fields(self):
        fields = wiki_check.parse_infobox(INFOBOX)
        self.assertEqual("1000", fields["hp"])
        self.assertEqual("110%", fields["iceDmgMod"])

    def test_wilson_interval_contains_the_estimate(self):
        low, high = wiki_check.wilson(9, 16846)
        self.assertLess(low, 9 / 16846)
        self.assertGreater(high, 9 / 16846)


class CurrentLootTest(unittest.TestCase):
    def test_current_loot_table_rarities(self):
        text = "| loot = {{Loot Table\n |{{Loot Item|1-105|Gold Coin|common}}\n |{{Loot Item|Steel Shield|uncommon}}\n" \
               " |{{Loot Item|Dragon Trophy|very rare}}\n |{{Loot Item|Odd Item|rare?}}\n |{{Loot Item|Plain Item}}\n}}"
        self.assertEqual({"gold coin": "common", "steel shield": "uncommon", "dragon trophy": "very rare", "odd item": None, "plain item": None},
                         wiki_check.parse_current_loot(text))


class MoneyTest(unittest.TestCase):
    def test_money_per_kill_uses_value_chance_and_mean_amount(self):
        self.assertAlmostEqual(0.5 * 50.5 + 0.1 * 2 * 100, wiki_check.money_per_kill([("gold coin", 50000, 1, 100), ("platinum coin", 10000, 1, 3)]))

    def test_different_money_is_reported_with_ranges_from_the_recorded_totals(self):
        # 9900 gold drops totalling 396000 (mean 40 per drop) and 7667 platinum drops totalling 11500 (mean 1.5).
        blocks = [{"version": "14.0", "kills": 10000, "items": {"gold coin": (9900, 1, 120), "platinum coin": (7667, 1, 3)},
                   "totals": {"gold coin": 396000, "platinum coin": 11500}}]
        rich = monster(loot=[{"name": "gold coin", "chance": 50000, "maxCount": 205}])
        findings, _ = wiki_check.compare(rich, {}, blocks, BY_NAME, BY_ID)
        coins = [f for f in findings if f["kind"] == "coins"]
        self.assertEqual(1, len(coins))
        self.assertEqual([["gold coin", 99000, 1, 79], ["platinum coin", 76670, 1, 2]], coins[0]["wiki"])
        self.assertEqual(round(396000 / 10000 + 11500 / 10000 * 100), coins[0]["wiki_money"])
        written = wiki_check.money_per_kill([tuple(c) for c in coins[0]["wiki"]])
        self.assertLess(abs(written - coins[0]["wiki_money"]) / coins[0]["wiki_money"], 0.05)

    def test_small_samples_do_not_judge_money(self):
        blocks = [{"version": "14.0", "kills": 300, "items": {"gold coin": (290, 1, 120)}, "totals": {"gold coin": 11600}}]
        findings, _ = wiki_check.compare(monster(loot=[{"name": "gold coin", "chance": 50000, "maxCount": 205}]), {}, blocks, BY_NAME, BY_ID)
        self.assertFalse([f for f in findings if f["kind"] == "coins"])

    def test_totals_are_parsed(self):
        blocks = wiki_check.parse_loot_statistics(LOOT_STATISTICS)
        self.assertEqual(270000, next(b for b in blocks if b["version"] == "12.40")["totals"]["gold coin"])


class DatapackLootTest(unittest.TestCase):
    def test_duplicate_entries_combine_like_the_server_rolls(self):
        loot = wiki_check.datapack_loot([{"name": "gold coin", "chance": 50000, "maxCount": 50}, {"id": 3031, "chance": 50000, "maxCount": 30}], BY_ID)
        probability, max_count, entries = loot["gold coin"]
        self.assertAlmostEqual(0.75, probability)
        self.assertEqual(50, max_count)
        self.assertEqual(2, entries)


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.blocks = wiki_check.parse_loot_statistics(LOOT_STATISTICS)
        self.infobox = wiki_check.parse_infobox(INFOBOX)

    def test_matching_monster_has_only_the_missing_item(self):
        findings, used = wiki_check.compare(monster(), self.infobox, self.blocks, BY_NAME, BY_ID)
        self.assertEqual({("loot_missing", "rare gem")}, kinds(findings))
        self.assertEqual("12.40", used["version"])

    def test_chance_outside_the_interval_is_flagged(self):
        loot = monster()["loot"]
        loot[2] = {"name": "beast hide", "chance": 15000}
        findings, _ = wiki_check.compare(monster(loot=loot), self.infobox, self.blocks, BY_NAME, BY_ID)
        self.assertIn(("loot_chance", "beast hide"), kinds(findings))

    def test_max_count_below_the_observed_amount_is_flagged(self):
        loot = monster()["loot"]
        loot[0] = {"name": "gold coin", "chance": 90000, "maxCount": 40}
        findings, _ = wiki_check.compare(monster(loot=loot), self.infobox, self.blocks, BY_NAME, BY_ID)
        self.assertIn(("loot_count", "gold coin"), kinds(findings))

    def test_datapack_only_loot_is_reported_not_removed(self):
        loot = monster()["loot"] + [{"name": "event token", "chance": 1000}, {"name": "tiny relic", "chance": 5}]
        findings, _ = wiki_check.compare(monster(loot=loot), self.infobox, self.blocks, BY_NAME, BY_ID)
        self.assertIn(("loot_datapack_only", "event token"), kinds(findings))  # ~100 expected drops
        self.assertNotIn(("loot_datapack_only", "tiny relic"), kinds(findings))  # 0.5 expected: absence is normal

    def test_stats_elements_and_immunities(self):
        changed = monster(maxHealth=1100, elements=[{"type": "COMBAT_FIREDAMAGE", "percent": 50}],
                          immunities=[{"type": "paralyze", "condition": False}])
        findings, _ = wiki_check.compare(changed, self.infobox, self.blocks, BY_NAME, BY_ID)
        self.assertTrue({("stat", "maxHealth"), ("element", "fireDmgMod"), ("immunity", "paralyze")} <= kinds(findings))

    def test_more_drops_than_kills_is_reported_not_judged(self):
        broken = [{"version": "13.0", "kills": 1000, "items": {"gold coin": (1500, 1, 60)}}]
        findings, _ = wiki_check.compare(monster(), {}, broken, BY_NAME, BY_ID)
        self.assertIn(("wiki_inconsistent", "gold coin"), kinds(findings))
        self.assertNotIn(("loot_chance", "gold coin"), kinds(findings))

    def test_weak_statistics_do_not_judge_chances(self):
        weak = [{"version": "13.0", "kills": 20, "items": {"gold coin": (1, 1, 10), "rare gem": (19, 1, 1)}}]
        findings, used = wiki_check.compare(monster(), {}, weak, BY_NAME, BY_ID)
        self.assertFalse(used["strong"])
        self.assertFalse({k for k, _ in kinds(findings)} & {"loot_chance", "loot_missing", "loot_datapack_only"})


class FetchTest(unittest.TestCase):
    def test_redirects_missing_and_continue_are_mapped_to_requested_titles(self):
        responses = [
            {"continue": {"rvcontinue": "x"}, "query": {
                "normalized": [{"from": "sample beast", "to": "Sample beast"}],
                "redirects": [{"from": "Sample beast", "to": "Sample Beast"}],
                "pages": {"1": {"title": "Sample Beast", "revisions": [{"revid": 7, "timestamp": "t", "slots": {"main": {"*": "text"}}}]}}}},
            {"query": {"pages": {"-1": {"title": "Nobody", "missing": ""}}}},
        ]
        calls = []

        def opener(url):
            calls.append(url)
            return json.dumps(responses[len(calls) - 1]).encode()

        with tempfile.TemporaryDirectory() as cache:
            wiki_check.fetch_pages(["sample beast", "Nobody", "Ghost"], cache, opener=opener, sleep=lambda _: None)
            self.assertEqual(7, wiki_check.read_cached(cache, "sample beast")["revid"])
            self.assertTrue(wiki_check.read_cached(cache, "Nobody")["missing"])
            self.assertTrue(wiki_check.read_cached(cache, "Ghost")["not_returned"])
            self.assertIn("rvcontinue=x", calls[1])
            wiki_check.fetch_pages(["sample beast"], cache, opener=opener, sleep=lambda _: None)
            self.assertEqual(2, len(calls))  # cached, no new request


if __name__ == "__main__":
    unittest.main()
