#!/usr/bin/env python3
"""Bot swarm for load tests (Phase 25): many 15.25 clients playing on a local Canary server.

    python3 -I tools/loadtest/seed.py --apply             # once: the accounts and characters
    python3 -I tools/loadtest/swarm.py --bots 100 --minutes 10

Only the "bot" characters of build/loadtest/characters.json are used (one per account, as
maxPlayersOnlinePerAccount = 1 allows); the "manual" ones are left for people. Bots log in from
several loopback addresses (127.0.0.2, 127.0.0.3, ...), at most --per-ip from each and one login
per address every 0.7 s, so the server's per-IP limits apply as they would to real players instead of
being switched off.

Profiles (each bot keeps one):
- hunter: stands in a hunting area, walks around, heals itself and casts its area spell; asks to
  respawn now and then (0x0F) in case it died;
- walker, talker (typing indicator + chat), trader (bank talkactions), relogger (logs out and in);
- pathfinder: uses the rope in its ammo slot on tiles 3-7 squares away, so the server computes a path
  (the "use item with" A* of Phase 23);
- modern: packets of the current protocol only (quick-loot list, weapon proficiency, highscores,
  outfit window).

Latency buckets are the exporter's (the largest finite one is 10 ms in the 2026-11 build: the finer
bounds of src/lib/metrics/metrics.cpp do not reach the Prometheus output), so ">10000" means over
10 ms; the average comes from the exact sum.

At the end it compares two scrapes of the metrics endpoint (build with FEATURE_METRICS and
metricsEnablePrometheus = true) and writes build/loadtest/report-<time>.json and .md: latency per
opcode, slow packets, throttled or rejected requests, database lock waits and query latency.
"""

import argparse
import collections
import json
import math
import os
import random
import re
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tibia_client import GameConnection, LoginRefused, require_loopback  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, "build", "loadtest")

DIRECTIONS = ["n", "e", "s", "w"]
HEAL = {1: [(20, "exura gran"), (8, "exura")], 2: [(20, "exura gran"), (8, "exura")], 3: [(35, "exura san"), (8, "exura")],
        4: [(8, "exura ico")], 9: [(20, "exura gran"), (8, "exura")]}
AREA = {1: [(55, "exevo gran mas vis")], 2: [(55, "exevo gran mas tera")], 3: [(50, "exevo mas san")],
        4: [(90, "exori gran"), (35, "exori")], 9: [(35, "exori mas pug")]}
BASE = {1: 1, 2: 2, 3: 3, 4: 4, 5: 1, 6: 2, 7: 3, 8: 4, 9: 9, 10: 9}
PHRASES = ["hi", "anyone hunting?", "selling rope", "where is the depot", "nice", "brb", "lol", "trade?", "thanks", "gl"]
QUICK_LOOT_ITEMS = [3031, 3035, 3043, 266, 268, 236, 237, 3725, 3577, 3582, 5880, 5895, 5925, 9057, 11444]
ROPE, SLOT_AMMO = 3003, 10


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.counters = collections.Counter()
        self.refusals = collections.Counter()
        self.login_seconds = []

    def add(self, key, value=1):
        with self.lock:
            self.counters[key] += value

    def refused(self, reason):
        with self.lock:
            self.refusals[reason.split("\n")[0][:80]] += 1


class LoginGate:
    """At most one login per source address every `interval` seconds."""

    def __init__(self, interval):
        self.interval = interval
        self.lock = threading.Lock()
        self.next_at = collections.defaultdict(float)

    def wait(self, address):
        with self.lock:
            now = time.monotonic()
            at = max(now, self.next_at[address])
            self.next_at[address] = at + self.interval
        time.sleep(max(0.0, at - time.monotonic()))


def spell(table, vocation, level):
    for minimum, words in table.get(BASE[vocation], []):
        if level >= minimum:
            return words
    return None


class Bot(threading.Thread):
    def __init__(self, index, args, password, account, character, profile, source_ip, gate, stats, stop):
        super().__init__(name=f"bot-{index}", daemon=True)
        self.args, self.password, self.account, self.character = args, password, account, character
        self.profile, self.source_ip, self.gate, self.stats, self.stop = profile, source_ip, gate, stats, stop
        self.rng = random.Random(index)
        self.client = None
        self.next_keepalive = 0.0

    # --- connection ---------------------------------------------------------------------------------

    def retire(self):
        """Counts the packets of the current connection once, before it is replaced or dropped."""
        if self.client is not None:
            self.stats.add("packets_sent", self.client.packets_sent)
            self.client.close()
            self.client = None

    def login(self):
        self.retire()
        for attempt in range(3):
            if self.stop.is_set():
                return False
            self.gate.wait(self.source_ip)
            started = time.monotonic()
            try:
                self.client = GameConnection(self.args.host, self.args.port, self.account["email"], self.password,
                                             self.character["name"], source_ip=self.source_ip).connect()
                with self.stats.lock:
                    self.stats.login_seconds.append(time.monotonic() - started)
                self.stats.add("logins")
                return True
            except LoginRefused as refusal:
                self.stats.refused(str(refusal).split(": ", 1)[-1])
            except OSError as error:
                self.stats.refused(f"socket: {error.__class__.__name__}")
            self.stats.add("login_failures")
            self.stop.wait(3 + attempt * 5)
        return False

    def logout(self):
        if self.client is not None and not self.client.closed.is_set():
            self.client.logout()
            time.sleep(0.3)
            self.stats.add("logouts")
        self.retire()

    def run(self):
        if not self.login():
            return
        try:
            while not self.stop.is_set():
                if self.client is None:
                    return
                if self.client.closed.is_set():
                    self.stats.add(f"dropped:{self.profile}")
                    if not self.stop.wait(5) and not self.login():
                        return
                    continue
                now = time.monotonic()
                if now >= self.next_keepalive:
                    self.client.keepalive()  # also answers the server's ping
                    self.next_keepalive = now + 5
                getattr(self, f"act_{self.profile}")()
                self.stats.add(f"actions:{self.profile}")
                self.stop.wait(self.rng.uniform(0.4, 0.9))
        finally:
            self.logout()

    # --- profiles -----------------------------------------------------------------------------------

    def act_hunter(self):
        c, roll = self.character, self.rng.random()
        if roll < 0.35:
            self.client.walk(self.rng.choice(DIRECTIONS))
        elif roll < 0.65:
            words = spell(HEAL, c["vocation"], c["level"])
            if words:
                self.client.say(words)
        elif roll < 0.9:
            words = spell(AREA, c["vocation"], c["level"])
            self.client.say(words) if words else self.client.walk(self.rng.choice(DIRECTIONS))
        else:
            self.client.send(b"\x0F")  # respawn after death; ignored while alive

    def act_walker(self):
        if self.rng.random() < 0.8:
            self.client.walk(self.rng.choice(DIRECTIONS))
        else:
            self.client.turn(self.rng.choice(DIRECTIONS))

    def act_talker(self):
        roll = self.rng.random()
        if roll < 0.4:
            self.client.typing(True)
        elif roll < 0.8:
            self.client.typing(False)
            self.client.say(self.rng.choice(PHRASES))
        else:
            self.client.walk(self.rng.choice(DIRECTIONS))

    def act_trader(self):
        roll = self.rng.random()
        if roll < 0.3:
            self.client.say("!balance")
        elif roll < 0.5:
            self.client.say(f"!deposit {self.rng.randint(1, 50)}")
        elif roll < 0.7:
            self.client.say(f"!withdraw {self.rng.randint(1, 50)}")
        else:
            self.client.walk(self.rng.choice(DIRECTIONS))

    def act_relogger(self):
        if self.rng.random() < 0.04:  # about once every 15 s
            self.logout()
            self.stats.add("relogs")
            if not self.stop.wait(self.rng.uniform(1, 4)):
                self.login()
        else:
            self.client.walk(self.rng.choice(DIRECTIONS))

    def act_pathfinder(self):
        x, y, z = self.character["position"]
        angle, distance = self.rng.uniform(0, 2 * math.pi), self.rng.randint(3, 7)
        target = (x + round(math.cos(angle) * distance), y + round(math.sin(angle) * distance), z)
        self.client.use_item_with((0xFFFF, SLOT_AMMO, 0), ROPE, 0, target, 0, 0)
        self.stats.add("pathfinder_uses")

    def act_modern(self):
        roll = self.rng.random()
        if roll < 0.25:
            items = self.rng.sample(QUICK_LOOT_ITEMS, self.rng.randint(1, len(QUICK_LOOT_ITEMS)))
            self.client.quick_loot_list(self.rng.random() < 0.5, items)
        elif roll < 0.45:
            self.client.proficiency_list()
        elif roll < 0.7:
            self.client.highscores(page=self.rng.randint(1, 5), category=self.rng.choice([0, 1, 2, 5, 6]))
        elif roll < 0.8:
            self.client.outfit_window()
        else:
            self.client.walk(self.rng.choice(DIRECTIONS))


# --- metrics -------------------------------------------------------------------------------------

LINE = re.compile(r'^([a-zA-Z_:][a-zA-Z0-9_:]*)(?:\{(.*)\})?\s+([-+0-9.eEinfINFNa]+)$')


def scrape(url):
    try:
        text = urllib.request.urlopen(url, timeout=10).read().decode("utf-8", "replace")
    except OSError as error:
        print(f"metrics: {url} did not answer ({error.__class__.__name__}); the report will have no server numbers")
        return None
    samples = {}
    for line in text.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        name, labels, value = match.groups()
        labels = tuple(sorted((k, v) for k, v in re.findall(r'(\w+)="((?:[^"\\]|\\.)*)"', labels or "") if not k.startswith("otel_")))
        try:
            samples[(name, labels)] = samples.get((name, labels), 0.0) + float(value)
        except ValueError:
            continue
    return samples


def delta(before, after):
    if not before or not after:
        return {}
    return {key: value - before.get(key, 0.0) for key, value in after.items() if value - before.get(key, 0.0) != 0}


def quantile(buckets, q):
    """Upper bound of the bucket holding quantile q, from cumulative (le, count) pairs; ">N" when it
    is the +Inf bucket (the exporter's largest finite bucket is N)."""
    total = buckets[-1][1] if buckets else 0
    if total <= 0:
        return None
    finite = [le for le, _ in buckets if le != float("inf")]
    for le, count in buckets:
        if count >= q * total:
            return le if le != float("inf") else f">{finite[-1] if finite else 0:g}"
    return None


def latency_table(changes, metric, label):
    rows = collections.defaultdict(lambda: {"count": 0.0, "sum": 0.0, "buckets": collections.defaultdict(float)})
    for (name, labels), value in changes.items():
        labels = dict(labels)
        key = labels.get(label)
        if key is None:
            continue
        if name == f"{metric}_count":
            rows[key]["count"] += value
        elif name == f"{metric}_sum":
            rows[key]["sum"] += value
        elif name == f"{metric}_bucket":
            rows[key]["buckets"][float(labels["le"])] += value
    table = []
    for key, row in rows.items():
        if row["count"] <= 0:
            continue
        buckets = sorted(row["buckets"].items())
        table.append({label: key, "count": int(row["count"]), "avg_us": round(row["sum"] / row["count"], 1),
                      "p50_us": quantile(buckets, 0.5), "p99_us": quantile(buckets, 0.99), "max_us": quantile(buckets, 1.0)})
    return sorted(table, key=lambda r: -r["count"])


def counters(changes, names):
    found = collections.defaultdict(float)
    for (name, labels), value in changes.items():
        base = name[:-6] if name.endswith("_total") else name
        if base in names or name in names:
            found[f"{name}{dict(labels) if labels else ''}"] += value
    return dict(found)


# --- main ----------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7172)
    parser.add_argument("--bots", type=int, default=100)
    parser.add_argument("--minutes", type=float, default=10.0)
    parser.add_argument("--per-ip", type=int, default=10, help="bots per source address (127.0.0.2, .3, ...)")
    parser.add_argument("--profiles", default="walker,talker,trader,relogger,pathfinder,modern",
                        help="profiles of the temple characters, in turn (hunting characters are hunters)")
    parser.add_argument("--metrics", default="http://127.0.0.1:9464/metrics")
    parser.add_argument("--manifest", default=os.path.join(OUT_DIR, "characters.json"))
    args = parser.parse_args()
    require_loopback(args.host)

    manifest = json.load(open(args.manifest, encoding="utf-8"))
    accounts = [a for a in manifest["accounts"] if a["role"] == "bot"][:args.bots]
    profiles = args.profiles.split(",")
    stats, stop, gate = Stats(), threading.Event(), LoginGate(0.7)
    bots = []
    for index, account in enumerate(accounts):
        # Even accounts send their hunting character, odd ones their temple character.
        hunting = [c for c in account["characters"] if c["place"].startswith("hunt:")]
        temple = [c for c in account["characters"] if c["place"].startswith("temple:")]
        if index % 2 == 0 and hunting:
            character, profile = hunting[0], "hunter"
        else:
            character, profile = (temple or account["characters"])[0], profiles[(index // 2) % len(profiles)]
        source_ip = f"127.0.0.{2 + index // args.per_ip}"
        bots.append(Bot(index, args, manifest["password"], account, character, profile, source_ip, gate, stats, stop))

    before = scrape(args.metrics)
    started = time.monotonic()
    print(f"{len(bots)} bots ({collections.Counter(b.profile for b in bots)}) from {len({b.source_ip for b in bots})} addresses, {args.minutes} min")
    for bot in bots:
        bot.start()
    try:
        deadline = started + args.minutes * 60
        while time.monotonic() < deadline:
            time.sleep(min(30, max(0.1, deadline - time.monotonic())))
            online = sum(1 for b in bots if (c := b.client) is not None and not c.closed.is_set())
            print(f"  {time.monotonic() - started:6.0f}s online {online}/{len(bots)} logins {stats.counters['logins']} "
                  f"failures {stats.counters['login_failures']} relogs {stats.counters['relogs']}")
    except KeyboardInterrupt:
        print("interrupted")
    stop.set()
    for bot in bots:
        bot.join(timeout=10)
    time.sleep(2)
    after = scrape(args.metrics)
    changes = delta(before, after)

    logins = sorted(stats.login_seconds)
    report = {
        "bots": len(bots), "minutes": round((time.monotonic() - started) / 60, 2),
        "profiles": dict(collections.Counter(b.profile for b in bots)),
        "client": dict(stats.counters), "refusals": dict(stats.refusals),
        "login_seconds": {"median": logins[len(logins) // 2] if logins else None, "max": logins[-1] if logins else None},
        "packets": [r for r in latency_table(changes, "method_latency_microseconds", "method") if r["method"].startswith("packet_")],
        "methods": [r for r in latency_table(changes, "method_latency_microseconds", "method") if not r["method"].startswith("packet_")][:25],
        "database_lock": latency_table(changes, "lock_latency_microseconds", "scope"),
        "queries": latency_table(changes, "query_latency_microseconds", "truncated_query")[:20],
        "server_counters": counters(changes, {
            "slow_packets", "connections_refused", "talkaction_requests_throttled", "outfit_requests_throttled",
            "highscore_requests_rejected", "highscore_requests", "highscore_served_from_cache", "quickloot_requests_rejected",
            "typing_updates_dropped", "client_requests_throttled", "packet_log_lines_suppressed", "database_task_connection_waits_exhausted",
            "database_task_connections_replaced", "database_transaction_deadlocks", "connection_output_queue_overflow"}),
    }
    stamp = time.strftime("%Y%m%d-%H%M%S")
    os.makedirs(OUT_DIR, exist_ok=True)
    json_path = os.path.join(OUT_DIR, f"report-{stamp}.json")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1)
    md_path = os.path.join(OUT_DIR, f"report-{stamp}.md")
    with open(md_path, "w", encoding="utf-8") as handle:
        handle.write(markdown(report))
    print(markdown(report))
    print(f"-> {json_path}")
    return 0


def markdown(report):
    out = [f"# Load test: {report['bots']} bots, {report['minutes']} min", "",
           f"Profiles: {report['profiles']}", "", f"Client: {report['client']}", "",
           f"Login refusals: {report['refusals'] or 'none'}", "", f"Login time (s): {report['login_seconds']}", "",
           "## Packets (dispatcher time per client packet; µs bucket bounds)", "", "| opcode | count | avg µs | p50 | p99 | max |", "|---|---|---|---|---|---|"]
    out += [f"| {r['method']} | {r['count']} | {r['avg_us']} | {r['p50_us']} | {r['p99_us']} | {r['max_us']} |" for r in report["packets"]]
    out += ["", "## Database lock wait", "", "| scope | count | avg µs | p99 µs |", "|---|---|---|---|"]
    out += [f"| {r['scope']} | {r['count']} | {r['avg_us']} | {r['p99_us']} |" for r in report["database_lock"]]
    out += ["", "## Queries (top 20 by count)", "", "| query | count | avg µs | p99 µs |", "|---|---|---|---|"]
    out += [f"| `{r['truncated_query']}` | {r['count']} | {r['avg_us']} | {r['p99_us']} |" for r in report["queries"]]
    out += ["", "## Server counters", ""] + [f"- {k}: {v:g}" for k, v in sorted(report["server_counters"].items())] + [""]
    return "\n".join(out)


if __name__ == "__main__":
    sys.exit(main())
