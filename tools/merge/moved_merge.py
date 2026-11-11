#!/usr/bin/env python3
"""Function-level 3-way merge of a god-object file whose functions moved to services/components.

Example (game.cpp -> services):

    git checkout --ours -- src/game/game.cpp
    python3 tools/merge/moved_merge.py --src src/game/game.cpp \\
        --exclude-insert 'sendEffects(' \\
        src/game/game.cpp src/game/*/*_service.cpp src/creatures/*/*_service.cpp src/items/item_service.cpp

Upstream functions are matched by name + parameter types (class prefix ignored). When a
function exists in several files (delegate in game.cpp + body in a service), the copy most
similar to the base body is merged; the other copies are left alone, so run
``audit_moved.py`` afterwards to catch duplicated bodies.

Upstream code merged into a service/component still uses implicit ``this`` members: qualify
them with ``game_.`` / ``m_player.`` by hand (the compiler and ``audit_moved.py`` help).
New upstream functions are inserted into the first FILE (the "home" file) after their
upstream predecessor; use ``--exclude-insert`` for signature changes you port by hand.
"""

import argparse
import collections
import difflib
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cppchunks import chunks, norm  # noqa: E402
from mergelib import code_only, default_base, merge3, show, strip_class  # noqa: E402


def func_index(chunk_list):
    result = {}
    order = []
    for chunk in chunk_list:
        if chunk["kind"] == "func":
            key = strip_class(chunk["key"])
            if key not in result:
                result[key] = chunk
                order.append(key)
    return result, order


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", required=True, help="upstream path of the god-object file")
    parser.add_argument("--theirs", default="origin/main")
    parser.add_argument("--base", help="merge base (default: git merge-base HEAD THEIRS)")
    parser.add_argument("--exclude-insert", nargs="*", default=[], help="key prefixes of new upstream functions not to insert")
    parser.add_argument("files", nargs="+", help="home file first, then globs of files holding moved code")
    args = parser.parse_args()

    base_rev = args.base or default_base(args.theirs)
    base, _ = func_index(chunks(show(base_rev, args.src)))
    theirs, theirs_order = func_index(chunks(show(args.theirs, args.src)))

    home = args.files[0]
    files = {}
    for pattern in args.files:
        for path in sorted(glob.glob(pattern, recursive=True)) or [pattern]:
            if path not in files and os.path.exists(path):
                with open(path) as handle:
                    files[path] = chunks(handle.read())

    def locations():
        result = collections.defaultdict(list)
        for path, chunk_list in files.items():
            for i, chunk in enumerate(chunk_list):
                if chunk["kind"] == "func" and chunk["text"] is not None:
                    result[strip_class(chunk["key"])].append((path, i))
        return result

    def best_copy(key, locs):
        reference = code_only((base.get(key) or theirs[key])["text"])
        scored = [
            (difflib.SequenceMatcher(None, code_only(files[p][i]["text"]), reference, autojunk=False).ratio(), (p, i))
            for p, i in locs
        ]
        return max(scored)

    log = collections.defaultdict(list)
    located = locations()
    for key in theirs_order:
        theirs_chunk, base_chunk = theirs[key], base.get(key)
        if base_chunk is not None and norm(base_chunk["text"]) == norm(theirs_chunk["text"]):
            continue
        if key not in located:
            continue
        ratio, (path, i) = best_copy(key, located[key])
        ours_chunk = files[path][i]
        if base_chunk is None:
            if norm(code_only(ours_chunk["text"])) != norm(code_only(theirs_chunk["text"])):
                log["new_upstream_but_ours_differs"].append(f"{key} @ {path}")
            continue
        ours_code = code_only(ours_chunk["text"])
        if norm(ours_code) == norm(code_only(theirs_chunk["text"])):
            continue
        lead = ours_chunk["text"][: len(ours_chunk["text"]) - len(ours_code)]
        merged, conflicts = merge3(ours_code, code_only(base_chunk["text"]), code_only(theirs_chunk["text"]))
        ours_chunk["text"] = lead + merged
        tag = ("conflict" if conflicts else "clean") + ("" if path == home else "_moved")
        if len(located[key]) > 1:
            tag += "_has_duplicates"
        log[tag].append(f"{key} @ {path} (similarity {ratio:.2f})")

    for key in base:
        if key not in theirs and key in located:
            log["deleted_upstream_port_by_hand"].append(f"{key} @ {sorted({p for p, _ in located[key]})}")

    located = locations()
    for pos, key in enumerate(theirs_order):
        if key in located or key in base:
            continue
        if any(key.startswith(prefix) for prefix in args.exclude_insert):
            log["excluded_insert"].append(key)
            continue
        if theirs[key]["key"].startswith("ns:"):
            # Namespace-scoped helpers must land inside the matching namespace block.
            log["new_namespace_member_place_by_hand"].append(theirs[key]["key"])
            continue
        anchor = (home, len(files[home]) - 1)
        for prev in reversed(theirs_order[:pos]):
            in_home = [loc for loc in located.get(prev, []) if loc[0] == home]
            if in_home:
                anchor = in_home[0]
                break
        path, i = anchor
        files[path].insert(i + 1, dict(theirs[key]))
        located = locations()
        log["inserted_home"].append(key)

    for path, chunk_list in files.items():
        text = "\n".join(c["text"] for c in chunk_list if c["text"] is not None)
        with open(path) as handle:
            if handle.read() == text:
                continue
        with open(path, "w") as handle:
            handle.write(text)

    for category, keys in log.items():
        print(f"== {category}: {len(keys)}")
        for key in keys:
            print("   ", key)


if __name__ == "__main__":
    main()
