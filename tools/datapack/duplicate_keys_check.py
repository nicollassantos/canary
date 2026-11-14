"""Report numeric keys repeated inside one Lua table literal.

Lua keeps only the last value of a repeated key, so the earlier entry disappears without a warning
(a chest without its unique id, a lever without its action id).

    python3 -I tools/datapack/duplicate_keys_check.py data-otservbr-global/startup/tables/*.lua

Exits 1 when a file has a repeated key. Only `[number] = ...` keys are checked, at every nesting
level; comments (`--` and `--[[ ... ]]`) are ignored.
"""

import re
import sys

KEY = re.compile(r"\[\s*(-?\d+)\s*\]\s*=(?!=)")


def strip_comments(text):
    """Blank out comments and strings, keeping newlines so line numbers stay right."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("--", i):
            m = re.match(r"--\[(=*)\[", text[i:])
            if m:
                close = "]" + m.group(1) + "]"
                end = text.find(close, i + m.end())
                end = n if end == -1 else end + len(close)
            else:
                end = text.find("\n", i)
                end = n if end == -1 else end
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(" " * (j + 1 - i))
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def duplicates(path):
    text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
    found = []
    stack = [{}]
    line = 1
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\n":
            line += 1
        elif c == "{":
            stack.append({})
        elif c == "}":
            if len(stack) > 1:
                stack.pop()
        elif c == "[":
            m = KEY.match(text, i)
            # A table key follows "{", "," or ";"; anything else is an indexing assignment (t[1] = x).
            previous = text[:i].rstrip()[-1:] if m else ""
            if m and previous in ("{", ",", ";"):
                key = m.group(1)
                seen = stack[-1]
                if key in seen:
                    found.append((line, key, seen[key]))
                else:
                    seen[key] = line
                line += text.count("\n", i, m.end())
                i = m.end()
                continue
        i += 1
    return found


def main(paths):
    problems = 0
    for path in paths:
        for line, key, first in duplicates(path):
            problems += 1
            print(f"{path}:{line}: key [{key}] repeats the one at line {first}; the earlier entry is lost")
    print(f"{len(paths)} files, {problems} repeated keys")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
