#!/usr/bin/env python3
"""Audit that upstream changes to moved functions reached every full copy of their body.

For each function changed upstream (base -> theirs) in ``--src``, every non-delegate copy in
FILES must contain the lines upstream added (after normalising ``m_player.``, ``game_.``,
``g_game().`` and similar receivers). Misses usually mean a hunk landed in a dead duplicate
or was dropped while resolving a conflict.

    python3 tools/merge/audit_moved.py --src src/game/game.cpp \\
        'src/game/**/*.cpp' 'src/creatures/**/*.cpp' 'src/items/**/*.cpp'
"""

import argparse
import difflib
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cppchunks import chunks, norm  # noqa: E402
from mergelib import default_base, show, strip_class  # noqa: E402

RECEIVERS = ("m_player.", "game_.", "g_game().", "this->", "Game::", "InternalGame::", "config_.", "g_configManager().")
TRIVIAL = {"}", "{", "} else {", "break;", "return;"}


def normalise(line):
    line = " ".join(line.split())
    for receiver in RECEIVERS:
        line = line.replace(receiver, "")
    return line


def is_delegate(text):
    body = [l.strip() for l in text.split("\n") if l.strip() and not l.strip().startswith("//")]
    calls = [l for l in body if "Service->" in l or "Component." in l or "Component->" in l]
    return len(body) <= 6 and len(calls) == 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", required=True)
    parser.add_argument("--theirs", default="origin/main")
    parser.add_argument("--base", help="merge base (default: git merge-base HEAD THEIRS; run before committing the merge or pass --base)")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    base_rev = args.base or default_base(args.theirs)

    def index(text):
        result = {}
        for chunk in chunks(text):
            if chunk["kind"] == "func":
                result.setdefault(strip_class(chunk["key"]), chunk)
        return result

    base = index(show(base_rev, args.src))
    theirs = index(show(args.theirs, args.src))
    ours = {}
    for pattern in args.files:
        for path in sorted(glob.glob(pattern, recursive=True)):
            with open(path) as handle:
                for chunk in chunks(handle.read()):
                    if chunk["kind"] == "func":
                        ours.setdefault(strip_class(chunk["key"]), []).append((path, chunk))

    misses = 0
    for key, theirs_chunk in theirs.items():
        base_chunk = base.get(key)
        if base_chunk is None or norm(base_chunk["text"]) == norm(theirs_chunk["text"]):
            continue
        diff = difflib.unified_diff(base_chunk["text"].split("\n"), theirs_chunk["text"].split("\n"), n=0, lineterm="")
        added = [normalise(l[1:]) for l in diff if l.startswith("+") and not l.startswith("+++")]
        added = [l for l in added if len(l) > 3 and l not in TRIVIAL]
        for path, chunk in ours.get(key, []):
            if is_delegate(chunk["text"]):
                continue
            body = {normalise(l) for l in chunk["text"].split("\n")}
            missing = [l for l in added if l not in body]
            if missing:
                misses += 1
                print(f"## {key[:90]} @ {path}: {len(missing)} upstream line(s) missing")
                for line in missing[:8]:
                    print("     ", line[:160])
    print(f"functions with missing upstream lines: {misses}")
    return 1 if misses else 0


if __name__ == "__main__":
    sys.exit(main())
