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


def load_stackable(path):
    """Item ids whose appearance has the `cumulative` flag (Appearances.object = 1, id = 1,
    flags = 3, AppearanceFlags.cumulative = 6 in src/protobuf/appearances.proto)."""
    with open(path, "rb") as handle:
        data = handle.read()
    stackable = set()
    for field, wire, value in _fields(data):
        if field != 1 or wire != 2:
            continue
        item_id, cumulative = None, False
        for sub_field, sub_wire, sub_value in _fields(value):
            if sub_field == 1 and sub_wire == 0:
                item_id = sub_value
            elif sub_field == 3 and sub_wire == 2:
                for flag_field, flag_wire, flag_value in _fields(sub_value):
                    if flag_field == 6 and flag_wire == 0:
                        cumulative = bool(flag_value)
        if item_id is not None and cumulative:
            stackable.add(item_id)
    return stackable


# --- plan: report -> manifest ----------------------------------------------------------------

def plan(report, by_name, stackable, kinds=("max_count", "add_loot", "chance", "stat", "element", "immunity"), bosses=frozenset(), element_types=None):
    """Fixes for the chosen kinds plus the findings left for manual review, with the reason.

    Bosses (bosstiary) are always left for review: their loot comes from the reward chest, so the
    wiki's amounts and rates do not describe one roll of the loot table.
    """
    fixes, review = [], []
    for monster in report["monsters"]:
        if monster["name"] in bosses:
            for finding in monster["findings"]:
                if finding["kind"] in ("loot_count", "loot_missing", "loot_chance", "stat", "element", "immunity"):
                    review.append({"monster": monster["name"], "item": finding["subject"], "reason": "boss: reward chest loot, review by hand"})
            continue
        block = monster.get("loot_block") or {}
        source = {"version": block.get("version"), "kills": block.get("kills"), "loot_revid": monster.get("loot_revid")}
        same_creature = bool(monster.get("race_id")) and bool(monster.get("wiki_race_id")) and int(monster["race_id"]) == int(monster["wiki_race_id"])
        for finding in monster["findings"]:
            item = finding["subject"]
            ids = sorted(by_name.get(item, set()))
            if finding["kind"] == "loot_count" and "max_count" in kinds:
                old, new = finding["datapack_max"], finding["wiki_observed_max"]
                if not ids or not all(i in stackable for i in ids):
                    review.append({"monster": monster["name"], "item": item, "reason": "not stackable; maxCount has no effect", "wiki": new})
                elif new > max(old * MAX_COUNT_GROWTH, MAX_COUNT_FLOOR):
                    review.append({"monster": monster["name"], "item": item, "reason": f"jump from {old} to {new} is too large to trust", "wiki": new})
                else:
                    fixes.append({"kind": "max_count", "file": monster["file"], "monster": monster["name"], "item": item, "maxCount": new, "source": source})
            elif finding["kind"] in ("stat", "element", "immunity") and finding["kind"] in kinds:
                page = {"revid": monster.get("revid")}
                if not same_creature:
                    review.append({"monster": monster["name"], "item": finding["subject"], "reason": "race id missing or different from the wiki"})
                    continue
                if finding["kind"] == "stat":
                    ours, wiki = float(finding["datapack"]), float(finding["wiki"])
                    if ours <= 0 or wiki <= 0 or not (1 / STAT_FACTOR <= ours / wiki <= STAT_FACTOR):
                        review.append({"monster": monster["name"], "item": finding["subject"], "reason": f"stat gap from {ours:g} to {wiki:g} is too large to trust"})
                        continue
                    fixes.append({"kind": "stat", "file": monster["file"], "monster": monster["name"], "item": finding["subject"], "value": int(round(wiki)), "source": page})
                elif finding["kind"] == "element":
                    combat = (element_types or {}).get(finding["subject"])
                    if not combat:
                        review.append({"monster": monster["name"], "item": finding["subject"], "reason": "unknown element"})
                        continue
                    fixes.append({"kind": "element", "file": monster["file"], "monster": monster["name"], "item": combat, "percent": int(round(float(finding["wiki"]))), "source": page})
                else:
                    fixes.append({"kind": "immunity", "file": monster["file"], "monster": monster["name"], "item": finding["subject"], "condition": bool(finding["wiki"]), "source": page})
            elif finding["kind"] == "loot_chance" and "chance" in kinds:
                major = int((re.findall(r"\d+", str(source.get("version") or "")) or [0])[0])
                ours, wiki = finding["datapack_chance"], max(1, int(finding["wiki_chance"]))
                reason = None
                if major < MIN_ADD_VERSION:
                    reason = f"old statistics (version {source.get('version')})"
                elif item in CURRENCY:
                    reason = "currency: compare with the existing coin entries"
                elif finding.get("entries", 1) != 1:
                    reason = f"{finding.get('entries')} entries share the item; split by hand"
                elif not (1 / MAX_CHANCE_FACTOR <= ours / wiki <= MAX_CHANCE_FACTOR):
                    reason = f"gap from {ours} to {wiki} is too large to trust"
                if reason:
                    review.append({"monster": monster["name"], "item": item, "reason": reason})
                else:
                    fixes.append({"kind": "chance", "file": monster["file"], "monster": monster["name"], "item": item, "chance": wiki,
                                  "source": {**source, "times": finding["times"], "kills": finding["kills"]}})
            elif finding["kind"] == "loot_missing" and "add_loot" in kinds:
                major = int((re.findall(r"\d+", str(source.get("version") or "")) or [0])[0])
                if item in CURRENCY:
                    review.append({"monster": monster["name"], "item": item, "reason": "currency: compare with the existing coin entries"})
                    continue
                if major < MIN_ADD_VERSION:
                    review.append({"monster": monster["name"], "item": item, "reason": f"old statistics (version {source.get('version')})"})
                    continue
                if len(ids) != 1:
                    review.append({"monster": monster["name"], "item": item, "reason": f"name resolves to {len(ids)} items", "ids": ids})
                    continue
                fix = {"kind": "add_loot", "file": monster["file"], "monster": monster["name"], "item": item, "id": ids[0],
                       "chance": max(1, int(finding["wiki_chance"])), "source": {**source, "times": finding["times"]}}
                if ids[0] in stackable and finding["max_amount"] > 1:
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


def set_numbers(text, patterns, value, flags=re.M):
    """Set every `patterns` match to value; None when a pattern is missing, else (text, changed)."""
    changed = False
    for pattern in patterns:
        match = re.search(pattern, text, flags)
        if not match:
            return None
        if match.group(2) != str(value):
            text = text[:match.start(2)] + str(value) + text[match.end(2):]
            changed = True
    return text, changed


def apply_attribute(text, fix):
    """Stat, element and immunity fixes; returns (text, outcome)."""
    if fix["kind"] == "stat":
        if fix["item"] == "armor":
            span = block_span(text, "defenses")
            if span is None:
                return text, "no monster.defenses block"
            inner = set_numbers(text[span[0]:span[1]], [r"^(\s*armor\s*=\s*)(\d+)"], fix["value"])
            if inner is None:
                return text, "no armor line"
            return text[:span[0]] + inner[0] + text[span[1]:], "applied" if inner[1] else "already"
        result = set_numbers(text, STAT_LINES.get(fix["item"], []), fix["value"]) if fix["item"] in STAT_LINES else None
        if result is None:
            return text, f"no {fix['item']} line"
        return result[0], "applied" if result[1] else "already"
    if fix["kind"] == "element":
        span = block_span(text, "elements")
        if span is None:
            return text, "no monster.elements block"
        inner = set_numbers(text[span[0]:span[1]], [rf"(\{{\s*type\s*=\s*{re.escape(fix['item'])}\s*,\s*percent\s*=\s*)(-?\d+)"], fix["percent"], flags=0)
        if inner is None:
            return text, "element not listed"
        return text[:span[0]] + inner[0] + text[span[1]:], "applied" if inner[1] else "already"
    span = block_span(text, "immunities")
    if span is None:
        return text, "no monster.immunities block"
    body = text[span[0]:span[1]]
    pattern = rf'(\{{\s*type\s*=\s*"{re.escape(fix["item"])}"\s*,\s*condition\s*=\s*)(true|false)'
    match = re.search(pattern, body)
    if not match:
        return text, "immunity not listed"
    wanted = "true" if fix["condition"] else "false"
    if match.group(2) == wanted:
        return text, "already"
    body = body[:match.start(2)] + wanted + body[match.end(2):]
    return text[:span[0]] + body + text[span[1]:], "applied"


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
            if (entry_number(body, "maxCount") or 1) >= fix["maxCount"]:
                outcomes.append("already")
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
            if entry_number(body, "chance") == fix["chance"]:
                outcomes.append("already")
                continue
            body = re.sub(r"\bchance\s*=\s*\d+", f"chance = {fix['chance']}", body, count=1)
            lines[index] = f"{match.group('indent')}{{{body}}}{match.group('tail')}"
            outcomes.append("applied")
        elif fix["kind"] == "add_loot":
            if matching:
                outcomes.append("already")
                continue
            indent = entries[0][1].group("indent") if entries else "\t"
            count = f", maxCount = {fix['maxCount']}" if fix.get("maxCount") else ""
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


def write_manifest(path, fixes, review, note):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"note": note, "fixes": fixes, "review": review}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")
