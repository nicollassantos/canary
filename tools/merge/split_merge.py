#!/usr/bin/env python3
"""Function-level 3-way merge of an upstream file that our branch split into several files.

Example (protocolgame was split into parse / send / helpers):

    git checkout --ours -- src/server/network/protocol/protocolgame.cpp
    python3 tools/merge/split_merge.py \\
        --src src/server/network/protocol/protocolgame.cpp \\
        --ours src/server/network/protocol/protocolgame.cpp \\
               src/server/network/protocol/protocolgame_send.cpp \\
               src/server/network/protocol/protocolgame_helpers.hpp \\
        --helpers src/server/network/protocol/protocolgame_helpers.hpp

Every upstream chunk (function, declaration, anonymous-namespace helper) is matched by
key to the file that holds it today and updated in place:

* ours == base      -> take theirs
* both changed      -> ``git merge-file`` on the chunk (conflicts marked ours/base/theirs)
* new upstream      -> inserted after its upstream predecessor (helpers go to ``--helpers``)
* deleted upstream  -> removed

Review every ``merged_*`` chunk and the placement of inserted chunks afterwards.
"""

import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cppchunks import chunks, norm  # noqa: E402
from mergelib import default_base, merge3, show  # noqa: E402

KINDS = ("func", "decl", "block")


def index(chunk_list):
    result = {}
    for chunk in chunk_list:
        if chunk["kind"] in KINDS:
            result.setdefault(chunk["key"], chunk)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", required=True, help="upstream path of the split file")
    parser.add_argument("--ours", nargs="+", required=True, help="files in our tree holding the split code")
    parser.add_argument("--helpers", required=True, help="file whose anonymous namespace receives new upstream helpers")
    parser.add_argument("--theirs", default="origin/main")
    parser.add_argument("--base", help="merge base (default: git merge-base HEAD THEIRS)")
    parser.add_argument("--mid", help="optional older upstream revision used as base for chunks missing from --base")
    args = parser.parse_args()

    base_rev = args.base or default_base(args.theirs)
    base = index(chunks(show(base_rev, args.src)))
    mid = index(chunks(show(args.mid, args.src))) if args.mid else {}
    theirs_chunks = chunks(show(args.theirs, args.src))
    theirs = index(theirs_chunks)

    files = {}
    for path in args.ours:
        with open(path) as handle:
            files[path] = chunks(handle.read())

    def ours_index():
        result = {}
        for path, chunk_list in files.items():
            for i, chunk in enumerate(chunk_list):
                if chunk["kind"] in KINDS and chunk["text"] is not None:
                    result.setdefault(chunk["key"], (path, i))
        return result

    log = collections.defaultdict(list)
    located = ours_index()

    for key, theirs_chunk in theirs.items():
        if key not in located:
            continue
        path, i = located[key]
        ours_chunk = files[path][i]
        ours_text, theirs_text = norm(ours_chunk["text"]), norm(theirs_chunk["text"])
        if ours_text == theirs_text:
            continue
        base_chunk = base.get(key) or mid.get(key)
        if base_chunk is None:
            log["no_base_kept_ours"].append(key)
            continue
        if norm(base_chunk["text"]) == theirs_text:
            continue
        if ours_text == norm(base_chunk["text"]):
            ours_chunk["text"] = theirs_chunk["text"]
            log["take_theirs"].append(key)
            continue
        merged, conflicts = merge3(ours_chunk["text"], base_chunk["text"], theirs_chunk["text"])
        ours_chunk["text"] = merged
        log["merged_conflict" if conflicts else "merged_clean"].append(f"{key} @ {path}")

    for key in base:
        if key not in theirs and key in located:
            path, i = located[key]
            files[path][i]["text"] = None
            log["deleted"].append(f"{key} @ {path}")

    theirs_keys = [c["key"] for c in theirs_chunks if c["kind"] in KINDS]
    for pos, key in enumerate(theirs_keys):
        located = ours_index()
        if key in located:
            continue
        is_helper = key.startswith("ns:anon::")
        anchor = None
        for prev in reversed(theirs_keys[:pos]):
            if prev.startswith("ns:anon::") == is_helper and prev in located:
                anchor = located[prev]
                break
        if anchor is None:
            target = args.helpers if is_helper else args.ours[0]
            chunk_list = files[target]
            if is_helper:
                anchor = (target, next(i for i, c in enumerate(chunk_list) if c["kind"] == "nsopen" and c.get("ns") == "anon"))
            else:
                anchor = (target, len(chunk_list) - 2)
                log["no_anchor"].append(key)
        path, i = anchor
        files[path].insert(i + 1, dict(theirs[key]))
        log["inserted"].append(f"{key} -> {path}")

    for path, chunk_list in files.items():
        with open(path, "w") as handle:
            handle.write("\n".join(c["text"] for c in chunk_list if c["text"] is not None))

    for category, keys in log.items():
        print(f"== {category}: {len(keys)}")
        if category != "take_theirs":
            for key in keys:
                print("   ", key)


if __name__ == "__main__":
    main()
