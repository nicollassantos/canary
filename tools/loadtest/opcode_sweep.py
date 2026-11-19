#!/usr/bin/env python3
"""Sends packets with random payloads for every client opcode from one loadtest bot and reports what each
opcode costs the game thread (method_latency{method="packet_0xNN"} from the metrics build).

    python3 -I tools/loadtest/opcode_sweep.py [--bot 20] [--per-opcode 20] [--rate 20] [--seed 1]

A hostile client sends whatever it wants; this finds the opcodes whose parse or use case is expensive with
garbage input, and works as a fuzzer. Each opcode first gets boundary payloads (runs of 0xFF and 0x00, and
sub-actions 1-3 followed by 0xFF: counts at the top of their type), then random ones. After each opcode
the packets must show up as processed in the metrics (served by another thread): if the process is gone
or the dispatcher stopped processing (a hang: the port still accepts connections), the opcode and the last
payloads are printed and written to build/loadtest/opcode_sweep_last.json to reproduce it.

Needs the metrics build on 127.0.0.1:9464 and the loadtest characters (tools/loadtest/seed.py). Only
loopback servers are accepted (tibia_client.require_loopback). The bot's character changes (outfit, VIP
list, quest tracker, ...): re-run seed.py --apply afterwards.
"""

import argparse
import json
import os
import random
import re
import socket
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tibia_client import GameConnection, LoginRefused  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIPPED = {
    0x0A: "first message",
    0x14: "logout",
    0x0F: "enter game (login flow)",
}


def metrics():
    text = urllib.request.urlopen("http://127.0.0.1:9464/metrics", timeout=5).read().decode()
    sums, counts, buckets = {}, {}, {}
    for line in text.splitlines():
        match = re.search(r'method="packet_0x([0-9A-F]{2})"', line)
        if not match:
            continue
        opcode = int(match.group(1), 16)
        value = float(line.rsplit(" ", 1)[1])
        if line.startswith("method_latency_microseconds_sum"):
            sums[opcode] = value
        elif line.startswith("method_latency_microseconds_count"):
            counts[opcode] = value
        elif line.startswith("method_latency_microseconds_bucket"):
            bound = re.search(r'le="([^"]+)"', line).group(1)
            buckets.setdefault(opcode, []).append((float("inf") if bound == "+Inf" else float(bound), value))
    return sums, counts, buckets


def boundary_payloads(opcode):
    payloads = [bytes([opcode]) + b"\xff" * n for n in (1, 2, 3, 4, 6, 8, 16)]
    payloads.append(bytes([opcode]) + b"\x00" * 4)
    for action in (1, 2, 3):
        payloads += [bytes([opcode, action]) + b"\xff" * n for n in (2, 4, 8)]
    return payloads


def processed(opcode):
    return metrics()[1].get(opcode, 0)


def server_alive():
    try:
        socket.create_connection(("127.0.0.1", 7172), timeout=2).close()
        return True
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bot", type=int, default=20, help="index of the loadtest bot account")
    parser.add_argument("--per-opcode", type=int, default=20, help="packets per opcode")
    parser.add_argument("--rate", type=float, default=20.0, help="packets per second (the server drops a connection above 25)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-payload", type=int, default=48, help="largest random payload, in bytes")
    parser.add_argument("--opcodes", default="", help="comma separated hex opcodes (default: all)")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    manifest = json.load(open(os.path.join(ROOT, "build", "loadtest", "characters.json")))
    account = [a for a in manifest["accounts"] if a["role"] == "bot"][args.bot]
    character = next(c for c in account["characters"] if c["place"].startswith("temple:"))["name"]
    opcodes = [int(o, 16) for o in args.opcodes.split(",")] if args.opcodes else [o for o in range(256) if o not in SKIPPED]

    def connect():
        for attempt in range(5):
            try:
                return GameConnection("127.0.0.1", 7172, account["email"], manifest["password"], character).connect()
            except (LoginRefused, OSError):
                time.sleep(1.0 + attempt)
        raise SystemExit("could not log the bot in")

    before_sums, before_counts, before_buckets = metrics()
    connection = connect()
    time.sleep(1.0)
    gap = 1.0 / args.rate
    reconnects = {}
    recent = []
    def report(kind, opcode, suspect=None):
        dump = {"problem": kind, "opcode": f"0x{opcode:02X}", "last_payloads": recent}
        if suspect is not None:
            dump["first_unprocessed_payload"] = suspect
        with open(os.path.join(ROOT, "build", "loadtest", "opcode_sweep_last.json"), "w") as out:
            json.dump(dump, out, indent=2)
        print(f"{kind} after opcode 0x{opcode:02X}; last payloads in build/loadtest/opcode_sweep_last.json")

    for opcode in opcodes:
        start = processed(opcode)
        sent = 0
        sent_payloads = []
        payloads = boundary_payloads(opcode) + [bytes([opcode]) + rng.randbytes(rng.randint(0, args.max_payload)) for _ in range(args.per_opcode)]
        for payload in payloads:
            recent = (recent + [payload.hex()])[-10:]
            if not connection.send(payload) or connection.closed.is_set():
                if not server_alive():
                    report("SERVER DOWN", opcode)
                    return 2
                reconnects[opcode] = reconnects.get(opcode, 0) + 1
                time.sleep(1.0)
                connection = connect()
                time.sleep(0.5)
                start = processed(opcode)
                sent = 0
                sent_payloads = []
                continue
            sent += 1
            sent_payloads.append(payload.hex())
            time.sleep(gap)
        # Every packet goes through packet_watch, which counts it once it is done. A count that stops short of
        # what was sent, with the connection still open, means the dispatcher is stuck in one of them.
        last, last_change = processed(opcode), time.monotonic()
        while last - start < sent and time.monotonic() - last_change < 3.0:
            time.sleep(0.25)
            now_count = processed(opcode)
            if now_count != last:
                last, last_change = now_count, time.monotonic()
        if last - start < sent and not connection.closed.is_set():
            # Packets are processed in order: the first one not counted is where the dispatcher is stuck.
            report("DISPATCHER HANG", opcode, sent_payloads[int(last - start)])
            return 3
    time.sleep(1.5)
    connection.logout()
    connection.close()

    sums, counts, buckets = metrics()
    rows = []
    for opcode in opcodes:
        count = counts.get(opcode, 0) - before_counts.get(opcode, 0)
        if count <= 0:
            continue
        total = sums.get(opcode, 0) - before_sums.get(opcode, 0)
        # Cumulative buckets: the slowest packet is at most the smallest bound that holds all of this run's.
        previous = dict(before_buckets.get(opcode, []))
        bounds = sorted((bound, value - previous.get(bound, 0)) for bound, value in buckets.get(opcode, []))
        top = next((bound for bound, delta in bounds if delta >= count), float("inf"))
        rows.append((total / count, opcode, int(count), top))
    rows.sort(reverse=True)
    print("opcode  packets  average us  slowest packet at most (us)")
    for average, opcode, count, top in rows[:25]:
        print(f"0x{opcode:02X}    {count:7d}  {average:10.0f}  ({top:.0f})")
    if reconnects:
        print("disconnected while sending:", ", ".join(f"0x{o:02X} x{n}" for o, n in sorted(reconnects.items())))
    print("server alive:", server_alive())
    return 0


if __name__ == "__main__":
    sys.exit(main())
