"""Split clang-formatted C++ source into top-level chunks (functions, namespace members).

Used by the move-aware merge tools in this directory. Joining every chunk's
``text`` with ``"\n"`` reproduces the original file exactly.

A chunk = leading comments/blank lines + declaration through matching closing brace
(or terminating ';' for declarations). Namespace blocks are descended into, and
their members get key prefix 'ns:<name>::'.
"""
import re

SIG_NAME = re.compile(r"([~A-Za-z_][\w:<>~]*(?:operator\S*)?)\s*\(")


def _strip_code(line, state):
    """Return line with strings/chars/comments blanked; state tracks block comments."""
    out = []
    i = 0
    n = len(line)
    in_block = state["block"]
    while i < n:
        c = line[i]
        if in_block:
            if line.startswith("*/", i):
                in_block = False
                i += 2
            else:
                i += 1
            continue
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if c == 'R' and line.startswith('R"', i):
            # raw string, assume single line delimiter-less
            m = re.match(r'R"([^(]*)\(', line[i:])
            if m:
                end = line.find(")" + m.group(1) + '"', i)
                if end != -1:
                    i = end + len(m.group(1)) + 2
                    out.append('""')
                    continue
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n:
                if line[j] == "\\":
                    j += 2
                    continue
                if line[j] == q:
                    break
                j += 1
            out.append(q + q)
            i = j + 1
            continue
        out.append(c)
        i += 1
    state["block"] = in_block
    return "".join(out)


def normalize_sig(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = " ".join(text.split())
    return text


def sig_key(header):
    """Build a key from the declaration header text (before '{')."""
    h = normalize_sig(header)
    # drop default args / attributes
    h = re.sub(r"\[\[[^\]]*\]\]", "", h).strip()
    paren = h.find("(")
    if paren == -1:
        return "decl:" + h
    m = None
    for m in SIG_NAME.finditer(h[: paren + 1]):
        pass
    name = m.group(1) if m else h[:paren]
    # parameter types: strip parameter names crudely by keeping full list normalized
    depth = 0
    end = paren
    for k in range(paren, len(h)):
        if h[k] == "(":
            depth += 1
        elif h[k] == ")":
            depth -= 1
            if depth == 0:
                end = k
                break
    params = h[paren + 1 : end]
    # remove param names: last identifier of each top-level comma segment
    segs, d, cur = [], 0, ""
    for ch in params:
        if ch in "<([":
            d += 1
        elif ch in ">)]":
            d -= 1
        if ch == "," and d == 0:
            segs.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        segs.append(cur)
    types = []
    for s in segs:
        s = s.split("=")[0].strip()
        s = re.sub(r"\s*([&*])\s*", r"\1 ", s).strip()
        parts = s.split()
        if len(parts) > 1 and re.fullmatch(r"[A-Za-z_]\w*", parts[-1]) and parts[-1] not in ("const", "int", "char", "bool", "long", "short", "unsigned", "float", "double"):
            parts = parts[:-1]
        types.append(" ".join(parts).replace(" &", "&").replace(" *", "*"))
    const = " const" if re.search(r"\)\s*const\b", h[end:]) else ""
    return f"{name}({', '.join(types)}){const}"


def chunks(text, prefix=""):
    """Yield dicts {key, text, kind} for top-level chunks of text."""
    lines = text.split("\n")
    state = {"block": False}
    res = []
    i = 0
    pending = []  # leading comment/blank lines
    n = len(lines)
    while i < n:
        line = lines[i]
        code = _strip_code(line, dict(state))
        stripped = line.strip()
        # pure comment / blank / preprocessor lines accumulate as leading
        if state["block"] or stripped == "" or stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
            _strip_code(line, state)
            pending.append(line)
            i += 1
            continue
        if stripped.startswith("#"):
            res.append({"key": f"{prefix}pp:{len(res)}:{stripped}", "text": "\n".join(pending + [line]), "kind": "pp"})
            pending = []
            i += 1
            continue
        mns = re.match(r"^\s*namespace\s*([\w:]*)\s*\{\s*$", line)
        if mns:
            # find matching close
            depth = 0
            j = i
            st = {"block": False}
            while j < n:
                c = _strip_code(lines[j], st)
                depth += c.count("{") - c.count("}")
                if depth == 0:
                    break
                j += 1
            inner = "\n".join(lines[i + 1 : j])
            nsname = mns.group(1) or "anon"
            res.append({"key": f"{prefix}nsopen:{nsname}:{len(res)}", "text": "\n".join(pending + [line]), "kind": "nsopen", "ns": nsname})
            pending = []
            res.extend(chunks(inner, prefix=f"{prefix}ns:{nsname}::"))
            res.append({"key": f"{prefix}nsclose:{nsname}:{len(res)}", "text": lines[j], "kind": "nsclose", "ns": nsname})
            i = j + 1
            continue
        # declaration / definition: accumulate until depth returns 0 after '{' or ';' at depth 0
        depth = 0
        j = i
        header = []
        opened = False
        st = dict(state)
        while j < n:
            c = _strip_code(lines[j], st)
            if not opened:
                header.append(c)
            for ch in c:
                if ch == "{":
                    depth += 1
                    opened = True
                elif ch == "}":
                    depth -= 1
            if opened and depth == 0:
                break
            if not opened and c.rstrip().endswith(";") and depth == 0:
                break
            j += 1
        state = st
        body = lines[i : j + 1]
        htxt = "\n".join(header)
        if opened:
            htxt = htxt[: htxt.find("{")] if "{" in htxt else htxt
        key = prefix + sig_key(htxt)
        kind = "func" if "(" in htxt and opened else ("decl" if not opened else "block")
        res.append({"key": key, "text": "\n".join(pending + body), "kind": kind})
        pending = []
        i = j + 1
    if pending:
        res.append({"key": f"{prefix}trailer:{len(res)}", "text": "\n".join(pending), "kind": "trailer"})
    return res


def norm(t):
    """Normalise a chunk for equality checks (trailing whitespace, outer blank lines)."""
    return "\n".join(l.rstrip() for l in t.strip("\n").split("\n"))
