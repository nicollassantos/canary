"""Shared helpers for the move-aware merge tools."""

import os
import re
import subprocess
import tempfile


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def show(rev, path):
    """Return ``path`` at ``rev`` or an empty string when it does not exist there."""
    result = subprocess.run(["git", "show", f"{rev}:{path}"], capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else ""


def default_base(theirs):
    return git("merge-base", "HEAD", theirs).strip()


def merge3(ours, base, theirs):
    """Three-way merge three snippets with ``git merge-file``.

    Returns ``(text, conflicts)``; conflict markers use the labels ours/base/theirs.
    """
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for name, text in (("ours", ours), ("base", base), ("theirs", theirs)):
            path = os.path.join(tmp, name)
            with open(path, "w") as handle:
                handle.write(text + "\n")
            paths.append(path)
        result = subprocess.run(
            ["git", "merge-file", "-p", "-L", "ours", "-L", "base", "-L", "theirs", *paths],
            capture_output=True,
            text=True,
        )
        out = result.stdout[:-1] if result.stdout.endswith("\n") else result.stdout
        return out, result.returncode


def _norm_params(key):
    match = re.match(r"^(.*?\()(.*)(\).*)$", key)
    if not match:
        return key
    params = [p.strip() for p in match.group(2).split(", ")] if match.group(2) else []
    # Top-level const on by-value parameters is not part of the signature.
    params = [p[6:] if p.startswith("const ") and not p.endswith(("&", "*")) else p for p in params]
    return match.group(1) + ", ".join(params) + match.group(3)


def strip_class(key):
    """Map ``Game::foo(int)`` / ``ns:anon::foo(int)`` to ``foo(int)`` so moved code can be matched."""
    key = re.sub(r"^ns:[^:]*::", "", key)
    match = re.match(r"^([\w:<>~]+?)::(~?\w+\(.*)$", key)
    return _norm_params(match.group(2) if match else key)


def code_only(text):
    """Drop the leading comment/blank block of a chunk."""
    lines = text.split("\n")
    start = next((i for i, line in enumerate(lines) if line.strip() and not line.strip().startswith(("//", "/*", "*"))), 0)
    return "\n".join(lines[start:])
