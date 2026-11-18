#!/usr/bin/env python3
"""Relog race check for the asynchronous logout save (standard library only).

Each cycle logs a bot character in, says "!withdraw 10" (bank -> 10 gold coins in the backpack), logs
out and logs in again at once. The logout save is committed on another connection; if a login could
load the character before that commit, it would see the old bank balance and the money would be
duplicated. After N cycles the database must hold exactly balance - 10 * N and 10 * N more gold.

    python3 -I tools/loadtest/relog_race_check.py --cycles 20

Uses the first bot account of build/loadtest/characters.json (its temple character), the database of
config.lua through `docker exec mysql`, and a server on 127.0.0.1:7172. Exit code 0 when it holds.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tibia_client import GameConnection  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GOLD_COIN = 3031


def query(sql):
    config = open(os.path.join(ROOT, "config.lua"), encoding="utf-8").read()
    password = re.search(r'^mysqlPass\s*=\s*"([^"]*)"', config, re.M).group(1)
    database = re.search(r'^mysqlDatabase\s*=\s*"([^"]*)"', config, re.M).group(1)
    user = re.search(r'^mysqlUser\s*=\s*"([^"]*)"', config, re.M).group(1)
    done = subprocess.run(["docker", "exec", "-e", "MYSQL_PWD", "mysql", "mysql", f"-u{user}", "-N", database, "-e", sql],
                          capture_output=True, text=True, env=dict(os.environ, MYSQL_PWD=password), check=True)
    return done.stdout.strip()


def money(name):
    escaped = name.replace("'", "''")
    balance = int(query(f"SELECT `balance` FROM `players` WHERE `name` = '{escaped}'"))
    gold = query(f"SELECT COALESCE(SUM(i.`count`), 0) FROM `player_items` i JOIN `players` p ON p.`id` = i.`player_id` "
                 f"WHERE p.`name` = '{escaped}' AND i.`itemtype` = {GOLD_COIN}")
    return balance, int(gold)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--port", type=int, default=7172)
    args = parser.parse_args()

    manifest = json.load(open(os.path.join(ROOT, "build", "loadtest", "characters.json"), encoding="utf-8"))
    account = next(a for a in manifest["accounts"] if a["role"] == "bot")
    character = next(c for c in account["characters"] if c["place"].startswith("temple:"))
    name = character["name"]

    balance_before, gold_before = money(name)
    print(f"{name}: balance {balance_before}, gold in items {gold_before}")
    client = None
    for cycle in range(args.cycles):
        client = GameConnection("127.0.0.1", args.port, account["email"], manifest["password"], name).connect()
        client.say("!withdraw 10")
        time.sleep(1.1)  # talkactions: one per second per player
        client.logout()
        client.close()
        # No pause: the next login races the logout save.
    time.sleep(1.0)  # let the last logout save commit

    balance_after, gold_after = money(name)
    print(f"after {args.cycles} cycles: balance {balance_after} ({balance_after - balance_before:+d}), "
          f"gold {gold_after} ({gold_after - gold_before:+d})")
    expected = 10 * args.cycles
    ok = balance_before - balance_after == expected and gold_after - gold_before == expected
    print("OK" if ok else f"FAIL: expected -{expected} balance and +{expected} gold")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
