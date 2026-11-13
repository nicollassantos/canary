"""Turn reviewed report findings into a fixes manifest and apply it to the monster files.

The manifest (tools/datapack/fixes.json) is the fork's record of datapack corrections: each
entry names the monster file, the item, the new value and the wiki sample it came from. `apply`
edits the files from the manifest only, so after an upstream merge the same manifest re-applies
on top of upstream's version ("take theirs, re-apply"). Applying twice changes nothing.

Kinds handled (chosen by the user, batch by batch):
- max_count: raise an item's maxCount to the largest amount the wiki observed (stackable items
  only: the server ignores maxCount for the rest);
- add_loot: add an item the wiki drops and the datapack lacks, when its name resolves to exactly
  one item;
- chance: set an item's chance to the wiki rate when the datapack falls outside the binomial
  interval (single entry only, recent statistics, no money, within MAX_CHANCE_FACTOR);
- stat (experience, maxHealth, armor), element (damage modifier) and immunity (paralyze,
  invisible): only when the datapack raceId equals the wiki race_id, so a quest variant that
  shares a name with another creature is never touched; stats also need a ratio within
  STAT_FACTOR and neither side at 0.
"""
import json
import os
import re

# A wiki amount far above the datapack's is more likely a page error or vandalism than a fix.
MAX_COUNT_GROWTH = 5
MAX_COUNT_FLOOR = 20
# Items are only added from statistics of this client version or newer: older samples describe
# loot tables Tibia has changed since.
MIN_ADD_VERSION = 12
# A chance is only changed within this factor of the wiki rate; beyond it the gap is more likely a
# different loot semantic (or a page error) than drift.
MAX_CHANCE_FACTOR = 5
# A chance beyond MAX_CHANCE_FACTOR is still corrected when the evidence is independent of a single
# page block: another version block agrees with the wiki rate (within CONFIRM_FACTOR) and none agrees
# with the datapack, or the only block is large (LARGE_SAMPLE drops over LARGE_KILLS kills).
CONFIRM_FACTOR = 2
LARGE_SAMPLE = 30
LARGE_KILLS = 1000
# Rarity classes of the wiki's current loot table (Template:Loot Item), as fractions; a rate within
# RARITY_TOLERANCE of a bound still counts as inside (the wiki rounds at the edges).
RARITY_RANGES = {"always": (1.0, 1.0), "common": (0.25, 1.0), "uncommon": (0.05, 0.25), "semi-rare": (0.01, 0.05),
                 "rare": (0.005, 0.01), "very rare": (0.0, 0.005)}
RARITY_TOLERANCE = 1.25


def in_rarity(rate, rarity):
    bounds = RARITY_RANGES.get(rarity or "")
    if bounds is None:
        return None
    low, high = bounds
    return low / RARITY_TOLERANCE <= rate <= min(1.0, high * RARITY_TOLERANCE)


def old_statistics_evidence(finding, ours=None):
    """For a finding from statistics older than MIN_ADD_VERSION: the current loot table's rarity is an
    independent, current check. True when the old rate fits it and the datapack chance (if any)
    does not; False when the datapack already fits; None when nothing can be said."""
    rarity = finding.get("current_rarity")
    wiki_fits = in_rarity(finding["wiki_chance"] / 100000, rarity)
    if wiki_fits is None:
        return None
    if ours is not None and in_rarity(ours / 100000, rarity):
        return False
    return bool(wiki_fits) or None


# Hit points, experience and armor change only within this factor (variants and quest copies show
# larger gaps).
STAT_FACTOR = 2
# Money is never added: the datapack pays high gold amounts as several gold coin entries where the
# wiki records platinum coins, so adding coins would raise a monster's money drop.
CURRENCY = frozenset({"gold coin", "platinum coin", "crystal coin"})


# --- appearances.dat (protobuf, decoded by hand: only object.id and object.flags.cumulative) ---

def _varint(data, pos):
    result = shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, pos
        shift += 7


def _fields(data):
    """Yield (field number, wire type, value) for one message; value is int or bytes."""
    pos = 0
    while pos < len(data):
        key, pos = _varint(data, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            value, pos = _varint(data, pos)
        elif wire == 2:
            length, pos = _varint(data, pos)
            value, pos = data[pos:pos + length], pos + length
        elif wire == 1:
            value, pos = data[pos:pos + 8], pos + 8
        elif wire == 5:
            value, pos = data[pos:pos + 4], pos + 4
        else:
            raise ValueError(f"unsupported wire type {wire}")
        yield field, wire, value


# AppearanceFlags fields in src/protobuf/appearances.proto: bools are set when non-zero,
# messages when present.
FLAG_FIELDS = {6: "cumulative", 18: "take", 36: "market"}


def load_flags(path):
    """Item id -> set of flag names (cumulative, take, market) from appearances.dat
    (Appearances.object = 1, Appearance.id = 1, Appearance.flags = 3)."""
    with open(path, "rb") as handle:
        data = handle.read()
    result = {}
    for field, wire, value in _fields(data):
        if field != 1 or wire != 2:
            continue
        item_id, flags = None, set()
        for sub_field, sub_wire, sub_value in _fields(value):
            if sub_field == 1 and sub_wire == 0:
                item_id = sub_value
            elif sub_field == 3 and sub_wire == 2:
                for flag_field, flag_wire, flag_value in _fields(sub_value):
                    name = FLAG_FIELDS.get(flag_field)
                    if name and ((flag_wire == 0 and flag_value) or flag_wire == 2):
                        flags.add(name)
        if item_id is not None:
            result[item_id] = flags
    return result


def load_stackable(path):
    """Item ids whose appearance has the `cumulative` flag."""
    return {item_id for item_id, flags in load_flags(path).items() if "cumulative" in flags}


def resolve_item(name, by_name, flags):
    """(item id, how) for a wiki item name, or (None, reason).

    An exact name with one id wins. Otherwise (several ids, or a wiki suffix such as "(item)" or
    "(green)" that items.xml lacks) the candidates are narrowed to items one can pick up and trade
    on the market, the kind a monster drops; exactly one must remain.
    """
    ids = sorted(by_name.get(name, set()))
    if len(ids) == 1:
        return ids[0], "name"
    if not ids:
        stripped = re.sub(r"\s*\([^)]*\)$", "", name)
        ids = sorted(by_name.get(stripped, set())) if stripped != name else []
        if not ids:
            return None, "name resolves to 0 items"
    loot_like = [i for i in ids if {"take", "market"} <= flags.get(i, set())]
    if len(loot_like) == 1:
        return loot_like[0], "flags"
    return None, f"name resolves to {len(ids)} items ({len(loot_like)} pickable and marketable)"


# --- plan: report -> manifest ----------------------------------------------------------------

LOOT_KINDS = ("max_count", "add_loot", "chance")
STAT_KEYS = {"experience": "exp", "maxHealth": "hp", "armor": "armor"}
IMMUNITY_KEYS = {"paralyze": "paraimmune", "invisible": "senseinvis"}


def identity_problem(race, wiki_race):
    """Why a loot fix cannot be trusted to target the wiki's creature, or None.

    The datapack must carry a race id (bestiary raceId or bosstiary bossRaceId): quest variants and
    event copies often reuse a regular creature's name without one. When the wiki page also has a
    race_id, the two must match.
    """
    if not race:
        return "creature identity not confirmed: the datapack has no race id"
    if wiki_race and int(race) != int(wiki_race):
        return "race id differs from the wiki"
    return None


def validate(fix, info, element_keys):
    """Re-check a manifest fix with the current rules; returns the reason it no longer holds, or None.

    `info` describes the fix's monster: race, wiki_race, reward (rewardBoss) and uncertain (infobox
    keys whose value the wiki marks with ? or ~). Unknown monsters are kept.
    """
    if info is None:
        return None
    if info.get("boss") and fix["kind"] == "stat" and not info.get("approved", {}).get((fix["kind"], fix["item"])):
        return BOSS_STAT_REASON
    if info.get("reward") and fix["kind"] in LOOT_KINDS:
        return "reward chest: chances are base values scaled per participant"
    if fix["kind"] in LOOT_KINDS:
        return identity_problem(info.get("race"), info.get("wiki_race"))
    race, wiki_race = info.get("race"), info.get("wiki_race")
    if not race or not wiki_race or int(race) != int(wiki_race):
        return "race id missing or different from the wiki"
    key = {"stat": STAT_KEYS, "immunity": IMMUNITY_KEYS}.get(fix["kind"], element_keys).get(fix["item"])
    if key in info.get("uncertain", ()):
        return "the wiki marks this value as uncertain"
    return None

def gap_evidence(finding):
    """Why a chance gap beyond MAX_CHANCE_FACTOR can be trusted, or None."""
    ours, wiki = finding["datapack_chance"] / 100000, max(1, finding["wiki_chance"]) / 100000
    rates = [times / kills for _, kills, times in finding.get("other_versions", []) if kills and times]
    near = lambda rate, target: target > 0 and 1 / CONFIRM_FACTOR <= rate / target <= CONFIRM_FACTOR
    if any(near(rate, ours) for rate in rates):
        return None
    if any(near(rate, wiki) for rate in rates):
        return "confirmed by another version"
    if not finding.get("other_versions") and finding["times"] >= LARGE_SAMPLE and finding["kills"] >= LARGE_KILLS:
        return "single large sample"
    return None


BOSS_STAT_REASON = "boss hit points, experience and armor can be tied to quest scripts and balance; review by hand"


def plan(report, by_name, stackable, kinds=("max_count", "add_loot", "chance", "stat", "element", "immunity"), bosses=frozenset(), element_types=None,
         reviewed=(), boss_names=frozenset(), flags=None):
    """`reviewed` holds case-by-case decisions (tools/datapack/reviewed.json): stat findings to apply
    despite the STAT_FACTOR rule, each with a note. Identity and certainty still apply (validate)."""
    approved = {(d["monster"], d["kind"], d["item"]) for d in reviewed if d.get("decision") == "apply"}
    """Fixes for the chosen kinds plus the findings left for manual review, with the reason.

    Loot of reward-chest monsters (`bosses`, flagged rewardBoss) is always left for review: each
    participant rolls the table with a contribution factor (2x for a solo kill), so the wiki's
    per-player amounts and rates do not describe one roll of the datapack table. Their stats,
    elements and immunities follow the normal rules.
    """
    fixes, review = [], []
    for monster in report["monsters"]:
        reward_chest = monster["name"] in bosses
        block = monster.get("loot_block") or {}
        source = {"version": block.get("version"), "kills": block.get("kills"), "loot_revid": monster.get("loot_revid")}
        same_creature = bool(monster.get("race_id")) and bool(monster.get("wiki_race_id")) and int(monster["race_id"]) == int(monster["wiki_race_id"])
        loot_identity = identity_problem(monster.get("race_id"), monster.get("wiki_race_id"))
        for finding in monster["findings"]:
            item = finding["subject"]
            ids = sorted(by_name.get(item, set()))
            if finding["kind"] in ("loot_count", "loot_chance", "loot_missing") and reward_chest:
                # Only the loot depends on the reward chest; stats, elements and immunities do not.
                review.append({"monster": monster["name"], "item": item, "reason": "reward chest: chances are base values scaled per participant"})
                continue
            if finding["kind"] in ("loot_count", "loot_chance", "loot_missing") and loot_identity:
                review.append({"monster": monster["name"], "item": item, "reason": loot_identity})
                continue
            if finding["kind"] == "loot_count" and "max_count" in kinds:
                old, new = finding["datapack_max"], finding["wiki_observed_max"]
                if not ids or not all(i in stackable for i in ids):
                    review.append({"monster": monster["name"], "item": item, "reason": "not stackable; maxCount has no effect", "wiki": new})
                elif new > max(old * MAX_COUNT_GROWTH, MAX_COUNT_FLOOR):
                    review.append({"monster": monster["name"], "item": item, "reason": f"jump from {old} to {new} is too large to trust", "wiki": new})
                else:
                    fixes.append({"kind": "max_count", "file": monster["file"], "monster": monster["name"], "item": item, "maxCount": new, "from": old, "source": source})
            elif finding["kind"] in ("stat", "element", "immunity") and finding["kind"] in kinds:
                page = {"revid": monster.get("revid")}
                if not same_creature:
                    review.append({"monster": monster["name"], "item": finding["subject"], "reason": "race id missing or different from the wiki"})
                    continue
                if finding["kind"] == "stat":
                    ours, wiki = float(finding["datapack"]), float(finding["wiki"])
                    manual = (monster["name"], "stat", finding["subject"]) in approved
                    if monster["name"] in boss_names and not manual:
                        review.append({"monster": monster["name"], "item": finding["subject"], "reason": BOSS_STAT_REASON})
                        continue
                    if not manual and (ours <= 0 or wiki <= 0 or not (1 / STAT_FACTOR <= ours / wiki <= STAT_FACTOR)):
                        review.append({"monster": monster["name"], "item": finding["subject"], "reason": f"stat gap from {ours:g} to {wiki:g} is too large to trust"})
                        continue
                    fixes.append({"kind": "stat", "file": monster["file"], "monster": monster["name"], "item": finding["subject"], "value": int(round(wiki)),
                                  "from": int(round(ours)), "source": page})
                elif finding["kind"] == "element":
                    combat = (element_types or {}).get(finding["subject"])
                    if not combat:
                        review.append({"monster": monster["name"], "item": finding["subject"], "reason": "unknown element"})
                        continue
                    fixes.append({"kind": "element", "file": monster["file"], "monster": monster["name"], "item": combat, "percent": int(round(float(finding["wiki"]))),
                                  "from": int(round(float(finding["datapack"]))), "source": page})
                else:
                    fixes.append({"kind": "immunity", "file": monster["file"], "monster": monster["name"], "item": finding["subject"], "condition": bool(finding["wiki"]),
                                  "from": bool(finding["datapack"]), "source": page})
            elif finding["kind"] == "loot_chance" and "chance" in kinds:
                major = int((re.findall(r"\d+", str(source.get("version") or "")) or [0])[0])
                ours, wiki = finding["datapack_chance"], max(1, int(finding["wiki_chance"]))
                reason, evidence = None, []
                if major < MIN_ADD_VERSION:
                    verdict = old_statistics_evidence(finding, ours)
                    if verdict is True:
                        evidence.append(f"old statistics within the current rarity ({finding.get('current_rarity')})")
                    elif verdict is False:
                        reason = f"old statistics (version {source.get('version')}); the datapack already fits the current rarity"
                    else:
                        reason = f"old statistics (version {source.get('version')})"
                if not reason and item in CURRENCY:
                    reason = "currency: compare with the existing coin entries"
                if not reason and finding.get("entries", 1) != 1:
                    reason = f"{finding.get('entries')} entries share the item; split by hand"
                if not reason and not (1 / MAX_CHANCE_FACTOR <= ours / wiki <= MAX_CHANCE_FACTOR):
                    gap = gap_evidence(finding)
                    if gap:
                        evidence.append(gap)
                    else:
                        reason = f"gap from {ours} to {wiki} is too large to trust and not confirmed by another version"
                if reason:
                    review.append({"monster": monster["name"], "item": item, "reason": reason})
                else:
                    fix_source = {**source, "times": finding["times"], "kills": finding["kills"]}
                    if evidence:
                        fix_source["evidence"] = "; ".join(evidence)
                    fixes.append({"kind": "chance", "file": monster["file"], "monster": monster["name"], "item": item, "chance": wiki, "from": ours,
                                  "source": fix_source})
            elif finding["kind"] == "loot_missing" and "add_loot" in kinds:
                major = int((re.findall(r"\d+", str(source.get("version") or "")) or [0])[0])
                if item in CURRENCY:
                    review.append({"monster": monster["name"], "item": item, "reason": "currency: compare with the existing coin entries"})
                    continue
                fix_source = {**source, "times": finding["times"]}
                if major < MIN_ADD_VERSION:
                    if old_statistics_evidence(finding) is not True:
                        review.append({"monster": monster["name"], "item": item, "reason": f"old statistics (version {source.get('version')})"})
                        continue
                    fix_source["evidence"] = f"old statistics within the current rarity ({finding.get('current_rarity')})"
                item_id, how = resolve_item(item, by_name, flags or {})
                if item_id is None:
                    review.append({"monster": monster["name"], "item": item, "reason": how, "ids": ids})
                    continue
                fix = {"kind": "add_loot", "file": monster["file"], "monster": monster["name"], "item": item, "id": item_id,
                       "chance": max(1, int(finding["wiki_chance"])), "source": fix_source}
                if how != "name":
                    # The server resolves names to the first id, so an ambiguous or suffixed name is written by id.
                    fix["by_id"] = True
                if item_id in stackable and finding["max_amount"] > 1:
                    fix["maxCount"] = finding["max_amount"]
                fixes.append(fix)
    fixes.sort(key=lambda f: (f["file"], f["kind"], f["item"]))
    return fixes, review


# --- apply: manifest -> monster files ----------------------------------------------------------

LOOT_START = re.compile(r"^monster\.loot\s*=\s*\{", re.M)
ENTRY = re.compile(r"^(?P<indent>\s*)\{(?P<body>[^{}]*)\}(?P<tail>,?\s*(--.*)?)$")


def loot_span(text):
    """(start, end) of the inside of `monster.loot = { ... }`, or None."""
    match = LOOT_START.search(text)
    if not match:
        return None
    depth, pos = 1, match.end()
    while depth and pos < len(text):
        depth += {"{": 1, "}": -1}.get(text[pos], 0)
        pos += 1
    return match.end(), pos - 1


def entry_item(body, by_id):
    name = re.search(r'name\s*=\s*"([^"]*)"', body)
    if name:
        return name.group(1).strip().lower()
    item_id = re.search(r"\bid\s*=\s*(\d+)", body)
    return by_id.get(int(item_id.group(1))) if item_id else None


def entry_number(body, key):
    match = re.search(rf"\b{key}\s*=\s*(\d+)", body)
    return int(match.group(1)) if match else None


STAT_LINES = {
    "experience": [r"^(monster\.experience\s*=\s*)(\d+)"],
    "maxHealth": [r"^(monster\.health\s*=\s*)(\d+)", r"^(monster\.maxHealth\s*=\s*)(\d+)"],
}


def block_span(text, name):
    """(start, end) of the inside of `monster.<name> = { ... }`, or None."""
    match = re.search(rf"^monster\.{name}\s*=\s*\{{", text, re.M)
    if not match:
        return None
    depth, pos = 1, match.end()
    while depth and pos < len(text):
        depth += {"{": 1, "}": -1}.get(text[pos], 0)
        pos += 1
    return match.end(), pos - 1


def attribute_target(fix):
    """(scope block or None, [patterns], wanted value, textual kind) for a stat/element/immunity fix."""
    if fix["kind"] == "stat":
        if fix["item"] == "armor":
            return "defenses", [r"^(\s*armor\s*=\s*)(\d+)"], fix["value"]
        return None, STAT_LINES.get(fix["item"], []), fix["value"]
    if fix["kind"] == "element":
        return "elements", [rf"(\{{\s*type\s*=\s*{re.escape(fix['item'])}\s*,\s*percent\s*=\s*)(-?\d+)"], fix["percent"]
    return "immunities", [rf'(\{{\s*type\s*=\s*"{re.escape(fix["item"])}"\s*,\s*condition\s*=\s*)(true|false)'], fix["condition"]


def apply_attribute(text, fix):
    """Stat, element and immunity fixes; returns (text, outcome). The value changes only when the
    file still holds the fix's `from` value (or no `from` is recorded); otherwise the conflict is
    reported and the file left alone."""
    scope, patterns, wanted = attribute_target(fix)
    if not patterns:
        return text, f"no {fix['item']} line"
    start, end = 0, len(text)
    if scope:
        span = block_span(text, scope)
        if span is None:
            return text, f"no monster.{scope} block"
        start, end = span
    body = text[start:end]
    flags = re.M if fix["kind"] == "stat" else 0
    matches = [re.search(pattern, body, flags) for pattern in patterns]
    if not all(matches):
        return text, f"{fix['item']} not listed"
    as_value = (lambda raw: raw == "true") if fix["kind"] == "immunity" else int
    current = as_value(matches[-1].group(2))
    if current == wanted:
        return text, "already"
    if "from" in fix and current != fix["from"]:
        return text, f"changed upstream ({fix['item']} {matches[-1].group(2)})"
    rendered = ("true" if wanted else "false") if fix["kind"] == "immunity" else str(wanted)
    for pattern in patterns:
        match = re.search(pattern, body, flags)
        body = body[:match.start(2)] + rendered + body[match.end(2):]
    return text[:start] + body + text[end:], "applied"


def apply_to_text(text, fixes, by_id):
    """Apply one file's fixes; returns (new text, outcomes) with outcome per fix:
    'applied', 'already' or a failure reason."""
    outcomes = []
    for fix in fixes:
        if fix["kind"] in ("stat", "element", "immunity"):
            text, outcome = apply_attribute(text, fix)
            outcomes.append(outcome)
            continue
        span = loot_span(text)
        if span is None:
            outcomes.append("no monster.loot block")
            continue
        start, end = span
        lines = text[start:end].split("\n")
        entries = [(i, ENTRY.match(line)) for i, line in enumerate(lines)]
        entries = [(i, m) for i, m in entries if m]
        matching = [(i, m) for i, m in entries if entry_item(m.group("body"), by_id) == fix["item"]]
        if fix["kind"] == "max_count":
            if not matching:
                outcomes.append("item not in loot")
                continue
            index, match = max(matching, key=lambda im: entry_number(im[1].group("body"), "maxCount") or 1)
            body = match.group("body")
            current = entry_number(body, "maxCount") or 1
            if current >= fix["maxCount"]:
                outcomes.append("already")
                continue
            if "from" in fix and current != fix["from"]:
                outcomes.append(f"changed upstream (maxCount {current})")
                continue
            if entry_number(body, "maxCount") is not None:
                body = re.sub(r"\bmaxCount\s*=\s*\d+", f"maxCount = {fix['maxCount']}", body)
            else:
                body = re.sub(r"(\bchance\s*=\s*\d+)", rf"\1, maxCount = {fix['maxCount']}", body, count=1)
            lines[index] = f"{match.group('indent')}{{{body}}}{match.group('tail')}"
            outcomes.append("applied")
        elif fix["kind"] == "chance":
            if len(matching) != 1:
                outcomes.append("item not in loot" if not matching else "several entries")
                continue
            index, match = matching[0]
            body = match.group("body")
            current = entry_number(body, "chance")
            if current == fix["chance"]:
                outcomes.append("already")
                continue
            if "from" in fix and current != fix["from"]:
                outcomes.append(f"changed upstream (chance {current})")
                continue
            body = re.sub(r"\bchance\s*=\s*\d+", f"chance = {fix['chance']}", body, count=1)
            lines[index] = f"{match.group('indent')}{{{body}}}{match.group('tail')}"
            outcomes.append("applied")
        elif fix["kind"] == "add_loot":
            if fix.get("by_id"):
                matching = [(i, m) for i, m in entries if entry_number(m.group("body"), "id") == fix["id"]]
            if matching:
                outcomes.append("already")
                continue
            indent = entries[0][1].group("indent") if entries else "\t"
            count = f", maxCount = {fix['maxCount']}" if fix.get("maxCount") else ""
            if fix.get("by_id"):
                new_line = f'{indent}{{ id = {fix["id"]}, chance = {fix["chance"]}{count} }}, -- {by_id.get(fix["id"], fix["item"])}'
            else:
                new_line = f'{indent}{{ name = "{fix["item"]}", chance = {fix["chance"]}{count} }},'
            position = next((i for i, m in entries if (entry_number(m.group("body"), "chance") or 0) < fix["chance"]), None)
            if position is None:
                position = entries[-1][0] + 1 if entries else len(lines) - 1
                if not entries and lines == [""]:
                    lines = ["", ""]
                    position = 1
            lines.insert(position, new_line)
            outcomes.append("applied")
        else:
            outcomes.append(f"unknown kind {fix['kind']}")
            continue
        text = text[:start] + "\n".join(lines) + text[end:]
    return text, outcomes


def apply_manifest(manifest, root, by_id, write=True):
    """Apply every fix; returns {outcome: count} and the failures."""
    by_file = {}
    for fix in manifest["fixes"]:
        by_file.setdefault(fix["file"], []).append(fix)
    summary, failures = {}, []
    for path, fixes in sorted(by_file.items()):
        full = os.path.join(root, path)
        if not os.path.exists(full):
            summary["file missing"] = summary.get("file missing", 0) + len(fixes)
            failures += [{**f, "outcome": "file missing"} for f in fixes]
            continue
        with open(full, encoding="utf-8") as handle:
            text = handle.read()
        new_text, outcomes = apply_to_text(text, fixes, by_id)
        for fix, outcome in zip(fixes, outcomes):
            summary[outcome] = summary.get(outcome, 0) + 1
            if outcome not in ("applied", "already"):
                failures.append({**fix, "outcome": outcome})
        if write and new_text != text:
            with open(full, "w", encoding="utf-8") as handle:
                handle.write(new_text)
    return summary, failures


def merge_fixes(existing, new):
    """The manifest accumulates: applied fixes disappear from later reports, so a new plan keeps
    every earlier fix and only replaces one for the same file, kind and item."""
    key = lambda fix: (fix["file"], fix["kind"], fix["item"])
    merged = {key(fix): fix for fix in existing}
    merged.update({key(fix): fix for fix in new})
    return sorted(merged.values(), key=lambda f: (f["file"], f["kind"], f["item"]))


def write_manifest(path, fixes, note):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"note": note, "fixes": fixes}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")
