#!/usr/bin/env python3
"""Compare the monster datapack with TibiaWiki facts and write a review report.

Report only: nothing in the datapack is changed. Steps (each can run alone):

    python3 -I tools/datapack/wiki_check.py extract   # luajit extractor -> monsters.jsonl
    python3 -I tools/datapack/wiki_check.py fetch     # TibiaWiki pages -> cache (about 1 request/s)
    python3 -I tools/datapack/wiki_check.py report    # -> report.json and report.md

Output and cache live in build/datapack-wiki (gitignored); pass --out to change it.
Facts used: loot statistics (kills, times an item dropped, amounts), hit points, experience,
armor, mitigation, damage modifiers, race id, paralysis/invisibility flags. No wiki text is kept
in the repository.

How the server rolls loot (data/libs/functions/monstertype.lua): every entry rolls on its own,
`chance` is out of 100000, stackable counts are uniform in [minCount, maxCount], and entries for the
same item add up, so the chance of the item showing up is 1 - prod(1 - p_i).
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ElementTree

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
API = "https://tibia.fandom.com/api.php"
USER_AGENT = "canary-datapack-check/0.1 (datapack consistency report)"
BATCH = 50
REQUEST_INTERVAL = 1.0
MAX_CHANCE = 100000

# The newest loot statistics with at least this many kills are used: older blocks (8.6 has the
# largest samples) describe loot that has changed since, and the binomial interval already widens
# for small samples.
MIN_KILLS = 100
# A stat that disagrees on more than this share of the comparable monsters is a unit or formula
# question (mitigation: datapack ~0.63 x wiki), reported once instead of per monster.
SYSTEMATIC_SHARE = 0.5
# Two-sided z for the Wilson interval; wide on purpose, the report covers ~20k items.
Z = 3.29
# A wiki item missing from the datapack is reported once it dropped at least this often.
MIN_TIMES_MISSING = 5
# A datapack item absent from the wiki sample is reported only when the datapack chance would
# have produced at least this many drops there (seeing none then has probability below 0.1%).
MIN_EXPECTED_DROPS = 7

ELEMENTS = {
    "COMBAT_PHYSICALDAMAGE": "physicalDmgMod",
    "COMBAT_EARTHDAMAGE": "earthDmgMod",
    "COMBAT_FIREDAMAGE": "fireDmgMod",
    "COMBAT_DEATHDAMAGE": "deathDmgMod",
    "COMBAT_ENERGYDAMAGE": "energyDmgMod",
    "COMBAT_HOLYDAMAGE": "holyDmgMod",
    "COMBAT_ICEDAMAGE": "iceDmgMod",
    "COMBAT_LIFEDRAIN": "hpDrainDmgMod",
    "COMBAT_DROWNDAMAGE": "drownDmgMod",
}


# --- items.xml -----------------------------------------------------------------------------

def load_items(path):
    """Return (name -> set of ids, id -> name) from items.xml, names lowercased like the server.

    items.xml is the repository's own file; downloaded wiki data is JSON and never parsed as XML.
    """
    by_name, by_id = {}, {}
    for item in ElementTree.parse(path).getroot().iter("item"):
        name = (item.get("name") or "").strip().lower()
        if not name:
            continue
        if item.get("id"):
            ids = [int(item.get("id"))]
        elif item.get("fromid") and item.get("toid"):
            ids = list(range(int(item.get("fromid")), int(item.get("toid")) + 1))
        else:
            continue
        for item_id in ids:
            by_id[item_id] = name
            by_name.setdefault(name, set()).add(item_id)
    return by_name, by_id


# --- wikitext parsing ----------------------------------------------------------------------

def parse_infobox(text):
    """Top-level `| key = value` fields of a creature infobox (single-line values only)."""
    fields = {}
    for line in text.splitlines():
        match = re.match(r"\|\s*([A-Za-z_]+)\s*=\s*(.*?)\s*$", line)
        if match and match.group(1) not in fields:
            fields[match.group(1)] = match.group(2)
    return fields


def parse_number(value):
    if value is None:
        return None
    match = re.match(r"^\s*(-?\d+(?:[.,]\d+)?)", value.replace(",", ""))
    return float(match.group(1)) if match else None


def version_key(version):
    return tuple(int(part) for part in re.findall(r"\d+", version or ""))


def parse_loot_statistics(text):
    """Every {{Loot2}} block: version, kills and item -> (times, min amount, max amount)."""
    blocks = []
    for body in re.findall(r"\{\{Loot2(.*?)\}\}", text, flags=re.S):
        block = {"version": None, "kills": 0, "items": {}}
        for raw in body.split("\n"):
            line = raw.strip()
            if not line.startswith("|"):
                continue
            line = line[1:]
            if line.startswith("version="):
                block["version"] = line.split("=", 1)[1].strip()
                continue
            if line.startswith("kills="):
                block["kills"] = int(parse_number(line.split("=", 1)[1]) or 0)
                continue
            if "=" in line.split(",", 1)[0]:
                continue
            parts = [part.strip() for part in line.split(",")]
            name_parts, values = [], {}
            for part in parts:
                match = re.match(r"^(times|amount|total):\s*(.*)$", part)
                if match:
                    values[match.group(1)] = match.group(2)
                elif not values:
                    name_parts.append(part)
            name = ", ".join(name_parts).strip()
            if not name or name.lower() == "empty" or "times" not in values:
                continue
            low = high = 1
            if values.get("amount"):
                numbers = [int(n) for n in re.findall(r"\d+", values["amount"])]
                if numbers:
                    low, high = min(numbers), max(numbers)
            times = int(parse_number(values["times"]) or 0)
            previous = block["items"].get(name.lower())
            if previous:
                times += previous[0]
                low, high = min(low, previous[1]), max(high, previous[2])
            block["items"][name.lower()] = (times, low, high)
        blocks.append(block)
    return blocks


def choose_block(blocks, min_kills=MIN_KILLS):
    """Newest version with enough kills; otherwise the block with most kills (marked as weak)."""
    if not blocks:
        return None, False
    ordered = sorted(blocks, key=lambda b: version_key(b["version"]), reverse=True)
    for block in ordered:
        if block["kills"] >= min_kills:
            return block, True
    return max(blocks, key=lambda b: b["kills"]), False


def wilson(times, kills, z=Z):
    if kills <= 0:
        return 0.0, 1.0
    p = min(1.0, times / kills)
    denominator = 1 + z * z / kills
    centre = (p + z * z / (2 * kills)) / denominator
    margin = z * math.sqrt(p * (1 - p) / kills + z * z / (4 * kills * kills)) / denominator
    return max(0.0, centre - margin), min(1.0, centre + margin)


# --- datapack side -------------------------------------------------------------------------

def datapack_loot(loot, by_id):
    """item name -> (probability it shows up, max count, entries) combining duplicate entries."""
    result = {}
    for entry in loot or []:
        if not isinstance(entry, dict):
            continue
        if entry.get("id") is not None:
            name = by_id.get(int(entry["id"]), f"#{entry['id']}")
        else:
            name = str(entry.get("name", "")).strip().lower()
        if not name:
            continue
        chance = min(max(float(entry.get("chance") or 0), 0.0), MAX_CHANCE) / MAX_CHANCE
        max_count = int(entry.get("maxCount") or 1)
        miss, best, count = result.get(name, (1.0, 0, 0))
        result[name] = (miss * (1 - chance), max(best, max_count), count + 1)
    return {name: (1 - miss, best, count) for name, (miss, best, count) in result.items()}


# --- comparison ----------------------------------------------------------------------------

def compare(monster, infobox, blocks, by_name, by_id, checked=None):
    """Findings for one monster: list of dicts with kind, subject and details.

    `checked`, when given, counts per stat how many monsters could be compared at all.
    """
    findings = []
    checked = checked if checked is not None else {}

    def mark(subject):
        checked[subject] = checked.get(subject, 0) + 1

    def add(kind, subject, **details):
        findings.append({"kind": kind, "subject": subject, **details})

    if infobox:
        wiki_race = parse_number(infobox.get("race_id"))
        if wiki_race and monster.get("raceId") and int(wiki_race) != int(monster["raceId"]):
            add("race_id", "race id", datapack=monster["raceId"], wiki=int(wiki_race))
        for field, key in (("maxHealth", "hp"), ("experience", "exp"), ("armor", "armor")):
            wiki_value = parse_number(infobox.get(key))
            ours = monster.get(field)
            if wiki_value is not None and ours is not None:
                mark(field)
                if float(ours) != wiki_value:
                    add("stat", field, datapack=ours, wiki=wiki_value)
        wiki_mitigation = parse_number(infobox.get("mitigation"))
        if wiki_mitigation is not None and monster.get("mitigation") is not None:
            mark("mitigation")
            if abs(float(monster["mitigation"]) - wiki_mitigation) > 0.005:
                add("stat", "mitigation", datapack=monster["mitigation"], wiki=wiki_mitigation)
        for element in monster.get("elements") or []:
            key = ELEMENTS.get(str(element.get("type")))
            wiki_mod = parse_number(infobox.get(key)) if key else None
            if wiki_mod is None or element.get("percent") is None:
                continue
            expected = 100 - wiki_mod
            if float(element["percent"]) != expected:
                add("element", key, datapack=element["percent"], wiki=expected)
        immunities = {str(i.get("type")): bool(i.get("condition")) for i in monster.get("immunities") or [] if isinstance(i, dict)}
        for immunity, key in (("paralyze", "paraimmune"), ("invisible", "senseinvis")):
            flag = (infobox.get(key) or "").strip().lower()
            if flag in ("yes", "no") and immunity in immunities and immunities[immunity] != (flag == "yes"):
                add("immunity", immunity, datapack=immunities[immunity], wiki=flag == "yes")

    block, strong = choose_block(blocks)
    if not block:
        return findings, None

    ours = datapack_loot(monster.get("loot"), by_id)
    kills = block["kills"]
    for name, (times, _, high) in block["items"].items():
        if times > kills:
            # The page counts more drops than kills (merged or mistyped rows): nothing to judge.
            add("wiki_inconsistent", name, times=times, kills=kills)
            continue
        low_p, high_p = wilson(times, kills)
        observed = times / kills if kills else 0.0
        resolved = by_name.get(name, set())
        if name not in ours:
            if times >= MIN_TIMES_MISSING and strong:
                add("loot_missing", name, wiki_chance=round(observed * MAX_CHANCE), times=times, kills=kills,
                    max_amount=high, resolves_to=sorted(resolved))
            continue
        probability, max_count, entries = ours[name]
        if strong and not (low_p <= probability <= high_p):
            add("loot_chance", name, datapack_chance=round(probability * MAX_CHANCE), wiki_chance=round(observed * MAX_CHANCE),
                wiki_low=round(low_p * MAX_CHANCE), wiki_high=round(high_p * MAX_CHANCE), times=times, kills=kills, entries=entries)
        if high > max_count:
            add("loot_count", name, datapack_max=max_count, wiki_observed_max=high)
    if strong:
        for name, (probability, _, _) in ours.items():
            if name not in block["items"] and probability * kills >= MIN_EXPECTED_DROPS:
                add("loot_datapack_only", name, datapack_chance=round(probability * MAX_CHANCE), kills=kills)
    return findings, {"version": block["version"], "kills": kills, "strong": strong}


# --- fetching ------------------------------------------------------------------------------

def cache_path(cache_dir, title):
    safe = re.sub(r"[^A-Za-z0-9._-]", "_", title)
    return os.path.join(cache_dir, safe[:180] + ".json")


def fetch_pages(titles, cache_dir, opener=None, sleep=time.sleep):
    """Fetch wikitext for titles not cached yet; one JSON file per requested title."""
    os.makedirs(cache_dir, exist_ok=True)
    pending = [t for t in titles if not os.path.exists(cache_path(cache_dir, t))]
    opener = opener or (lambda url: urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT}), timeout=30).read())
    for start in range(0, len(pending), BATCH):
        batch = pending[start:start + BATCH]
        params = {"action": "query", "prop": "revisions", "rvprop": "content|ids|timestamp", "rvslots": "main",
                  "format": "json", "redirects": "1", "titles": "|".join(batch)}
        results, extra = {}, {}
        while True:
            data = json.loads(opener(API + "?" + urllib.parse.urlencode({**params, **extra})))
            query = data.get("query", {})
            alias = {}
            for step in query.get("normalized", []) + query.get("redirects", []):
                alias[step["to"]] = alias.get(step["from"], step["from"])
            for page in query.get("pages", {}).values():
                requested = alias.get(page.get("title"), page.get("title"))
                while requested in alias:
                    requested = alias[requested]
                if "missing" in page:
                    results[requested] = {"title": page.get("title"), "missing": True}
                    continue
                revision = (page.get("revisions") or [{}])[0]
                results[requested] = {
                    "title": page.get("title"),
                    "revid": revision.get("revid"),
                    "timestamp": revision.get("timestamp"),
                    "text": revision.get("slots", {}).get("main", {}).get("*", ""),
                }
            if "continue" not in data:
                break
            extra = data["continue"]
            sleep(REQUEST_INTERVAL)
        for title in batch:
            with open(cache_path(cache_dir, title), "w", encoding="utf-8") as handle:
                json.dump(results.get(title, {"title": title, "not_returned": True}), handle)
        sleep(REQUEST_INTERVAL)


def read_cached(cache_dir, title):
    path = cache_path(cache_dir, title)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


# --- commands ------------------------------------------------------------------------------

def command_extract(args):
    files = []
    for directory, _, names in os.walk(os.path.join(ROOT, args.monsters)):
        files += [os.path.join(directory, n) for n in names if n.endswith(".lua")]
    os.makedirs(args.out, exist_ok=True)
    target = os.path.join(args.out, "monsters.jsonl")
    with open(target, "w", encoding="utf-8") as out:
        run = subprocess.run(["luajit", os.path.join(ROOT, "tools/datapack/extract_monsters.lua")],
                             input="\n".join(sorted(files)) + "\n", text=True, stdout=out, stderr=subprocess.PIPE, check=False)
    errors = [line for line in run.stderr.splitlines() if line.startswith("ERROR")]
    count = sum(1 for _ in open(target, encoding="utf-8"))
    print(f"{count} monsters -> {target}; {len(errors)} files failed")
    for line in errors:
        print("  " + line)
    return 0 if run.returncode == 0 else 1


def load_monsters(out_dir):
    with open(os.path.join(out_dir, "monsters.jsonl"), encoding="utf-8") as handle:
        return [json.loads(line) for line in handle]


def command_fetch(args):
    monsters = load_monsters(args.out)
    names = [m["name"] for m in monsters]
    cache = os.path.join(args.out, "cache")
    fetch_pages(names, os.path.join(cache, "creature"))
    fetch_pages([f"Loot Statistics:{n}" for n in names], os.path.join(cache, "loot"))
    print(f"cached {len(names)} creatures and their loot statistics in {cache}")
    return 0


def command_report(args):
    monsters = load_monsters(args.out)
    by_name, by_id = load_items(os.path.join(ROOT, args.items))
    cache = os.path.join(args.out, "cache")
    report = {"monsters": [], "not_on_wiki": [], "no_loot_statistics": [], "systematic": {}}
    checked = {}
    for monster in monsters:
        page = read_cached(os.path.join(cache, "creature"), monster["name"]) or {}
        loot_page = read_cached(os.path.join(cache, "loot"), f"Loot Statistics:{monster['name']}") or {}
        if not page.get("text"):
            report["not_on_wiki"].append(monster["name"])
            continue
        infobox = parse_infobox(page["text"])
        blocks = parse_loot_statistics(loot_page.get("text", ""))
        if not blocks and monster.get("loot"):
            report["no_loot_statistics"].append(monster["name"])
        findings, used = compare(monster, infobox, blocks, by_name, by_id, checked)
        if findings:
            report["monsters"].append({"name": monster["name"], "file": os.path.relpath(monster["file"], ROOT) if os.path.isabs(monster["file"]) else monster["file"],
                                       "revid": page.get("revid"), "loot_revid": loot_page.get("revid"), "loot_block": used, "findings": findings})
    move_systematic(report, checked)
    totals = {}
    for entry in report["monsters"]:
        for finding in entry["findings"]:
            key = finding["kind"] if finding["kind"] not in ("stat", "element") else f"{finding['kind']}:{finding['subject']}"
            totals[key] = totals.get(key, 0) + 1
    report["totals"] = totals
    report["compared"] = len(monsters) - len(report["not_on_wiki"])
    with open(os.path.join(args.out, "report.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1)
    with open(os.path.join(args.out, "report.md"), "w", encoding="utf-8") as handle:
        handle.write(render_markdown(report, len(monsters)))
    print(f"compared {report['compared']} of {len(monsters)} monsters; {len(report['monsters'])} with findings")
    for key, value in sorted(totals.items(), key=lambda kv: -kv[1]):
        print(f"  {key}: {value}")
    return 0


def move_systematic(report, checked):
    """Stats that disagree on most comparable monsters leave the per-monster lists."""
    mismatches = {}
    for entry in report["monsters"]:
        for finding in entry["findings"]:
            if finding["kind"] == "stat":
                mismatches.setdefault(finding["subject"], []).append(finding)
    for subject, found in mismatches.items():
        if checked.get(subject) and len(found) / checked[subject] > SYSTEMATIC_SHARE:
            ratios = sorted(f["datapack"] / f["wiki"] for f in found if f["wiki"])
            report["systematic"][subject] = {
                "mismatches": len(found), "compared": checked[subject],
                "median_ratio_datapack_over_wiki": round(ratios[len(ratios) // 2], 3) if ratios else None,
            }
            for entry in report["monsters"]:
                entry["findings"] = [f for f in entry["findings"] if not (f["kind"] == "stat" and f["subject"] == subject)]
    report["monsters"] = [entry for entry in report["monsters"] if entry["findings"]]


def render_markdown(report, total):
    lines = ["# Datapack vs TibiaWiki", "",
             f"Compared {report['compared']} of {total} monsters. Not on the wiki: {len(report['not_on_wiki'])}. "
             f"With loot but no loot statistics: {len(report['no_loot_statistics'])}.", "",
             "Chances are out of 100000. A chance is flagged only outside a wide binomial interval around "
             f"the wiki's times/kills (z = {Z}, newest block with at least {MIN_KILLS} kills).", "",
             "## Systematic differences (unit or formula questions, not per-monster errors)", ""]
    lines += [f"- {subject}: {info['mismatches']} of {info['compared']} differ; median datapack/wiki = {info['median_ratio_datapack_over_wiki']}"
              for subject, info in report["systematic"].items()] or ["- none"]
    lines += ["", "## Totals", ""]
    lines += [f"- {key}: {value}" for key, value in sorted(report["totals"].items(), key=lambda kv: -kv[1])]
    for entry in sorted(report["monsters"], key=lambda e: -len(e["findings"])):
        block = entry["loot_block"] or {}
        lines += ["", f"## {entry['name']}", "",
                  f"`{entry['file']}` · wiki revid {entry['revid']} · loot revid {entry['loot_revid']} · "
                  f"loot version {block.get('version')} ({block.get('kills')} kills{'' if block.get('strong', True) else ', weak'})", ""]
        for f in entry["findings"]:
            details = ", ".join(f"{k}={v}" for k, v in f.items() if k not in ("kind", "subject"))
            lines.append(f"- **{f['kind']}** {f['subject']}: {details}")
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["extract", "fetch", "report"])
    parser.add_argument("--out", default=os.path.join(ROOT, "build", "datapack-wiki"))
    parser.add_argument("--monsters", default="data-otservbr-global/monster")
    parser.add_argument("--items", default="data/items/items.xml")
    args = parser.parse_args(argv)
    return {"extract": command_extract, "fetch": command_fetch, "report": command_report}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
