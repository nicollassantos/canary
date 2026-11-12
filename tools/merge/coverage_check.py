#!/usr/bin/env python3
"""Check that every function of an upstream file has a copy in the branch files that hold it.

Run after moving code (and after each upstream merge) to prove the merge commands in
README.md still cover every upstream function:

    python3 tools/merge/coverage_check.py --src src/game/game.cpp \
        src/game/game.cpp src/game/*/*_service.cpp ...

Prints the upstream functions with no local copy and those with several copies. Functions are
matched by name and parameters without the class, like moved_merge.py does. Namespace
helpers are keyed separately and renamed ones never match; list them in README.md
("Namespace helpers") and pass --allow-unmatched for them.
"""

import argparse
import collections
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mergelib import default_base, show, strip_class  # noqa: E402
from moved_merge import chunks, func_index  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", required=True, help="upstream path of the file")
    parser.add_argument("--rev", help="upstream revision to read (default: merge base with origin/main)")
    parser.add_argument("--allow-unmatched", nargs="*", default=[], help="key prefixes that are known not to match")
    parser.add_argument("files", nargs="+", help="branch files (globs allowed) holding the code")
    args = parser.parse_args()

    upstream, _ = func_index(chunks(show(args.rev or default_base("origin/main"), args.src)))

    located = collections.defaultdict(list)
    for pattern in args.files:
        for path in sorted(glob.glob(pattern, recursive=True)) or [pattern]:
            if not os.path.exists(path):
                continue
            with open(path) as handle:
                for chunk in chunks(handle.read()):
                    if chunk["kind"] == "func" and chunk["text"] is not None:
                        located[strip_class(chunk["key"])].append(path)

    unmatched = [key for key in upstream if key not in located and not any(key.startswith(p) for p in args.allow_unmatched)]
    duplicates = {key: paths for key, paths in located.items() if key in upstream and len(paths) > 1}

    print(f"{args.src}: {len(upstream)} upstream functions, {len(unmatched)} without a local copy")
    for key in unmatched:
        print(f"  missing: {key}")
    for key, paths in sorted(duplicates.items()):
        print(f"  duplicate: {key} @ {', '.join(paths)}")
    return 1 if unmatched else 0


if __name__ == "__main__":
    sys.exit(main())
