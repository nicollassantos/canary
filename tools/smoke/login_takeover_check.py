#!/usr/bin/env python3
"""Login takeover check against a running server (8.60 classic game port, standard library only).

1. The victim logs in with the right password and stays connected.
2. Another connection sends the victim's character name with an unknown account
   and a wrong password. The victim must stay connected (before the fix the
   server dropped the victim's client before checking the password).
3. The owner logs in again with the right password: the old connection must be
   replaced (legitimate takeover still works).

Usage: login_takeover_check.py HOST PORT ACCOUNT PASSWORD CHARACTER
Exit code 0 when every expectation holds.

Run it against a server on a throwaway database (tasks/AGENTS.md section 7) with
`allowOldProtocol = true` and a fixed `legacy860GameProtocolPort`, after seeding an account
(SHA1 password, `name` = ACCOUNT) and a character on it; the server's working directory needs
`data`, `data-otservbr-global` and `key.pem`. Phase 23.5a: against the build before the fix the
victim was dropped (`victim alive=False`); after it, the check prints OK.
"""

import os
import socket
import struct
import sys
import threading
import time
import zlib

# Standard OT RSA key (src/security/rsa.cpp default, same modulus as key.pem).
P = 14299623962416399520070177382898895550795403345466153217470516082934737582776038882967213386204600674145392845853859217990626450972452084065728686565928113
Q = 7630979195970404721891201847792002125535401292779123937207447574596692788513647179235335529307251350570728407373705564708871762033017096809910315212884101
N = P * Q
E = 65537
CLIENTOS_WINDOWS = 2


def lua_string(text):
    raw = text.encode("latin-1")
    return struct.pack("<H", len(raw)) + raw


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("closed while reading")
        data += chunk
    return data


def read_challenge(sock):
    (length,) = struct.unpack("<H", recv_exact(sock, 2))
    body = recv_exact(sock, length)
    # adler32 (4) + inner length (2) + 0x1F + u32 timestamp + u8 random
    if body[6] != 0x1F:
        raise ValueError(f"unexpected challenge opcode 0x{body[6]:02X}")
    timestamp, random = struct.unpack("<IB", body[7:12])
    return timestamp, random


def first_message(account, password, character, timestamp, random):
    xtea = os.urandom(16)
    block = b"\x00" + xtea + b"\x00" + lua_string(account) + lua_string(character) + lua_string(password)
    block += struct.pack("<IB", timestamp, random)
    block += os.urandom(128 - len(block))
    encrypted = pow(int.from_bytes(block, "big"), E, N).to_bytes(128, "big")
    payload = b"\x0A" + struct.pack("<HH", CLIENTOS_WINDOWS, 860) + encrypted
    checksum = zlib.adler32(payload) & 0xFFFFFFFF
    body = struct.pack("<I", checksum) + payload
    return struct.pack("<H", len(body)) + body


class Session:
    """One game connection; a reader thread notes when the server closes it."""

    def __init__(self, host, port, account, password, character):
        self.closed_at = None
        self.received = 0
        self.sock = socket.create_connection((host, port), timeout=10)
        timestamp, random = read_challenge(self.sock)
        self.sock.sendall(first_message(account, password, character, timestamp, random))
        self.sock.settimeout(None)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                self.received += len(chunk)
        except OSError:
            pass
        self.closed_at = time.monotonic()

    @property
    def alive(self):
        return self.closed_at is None

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def main():
    host, port, account, password, character = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
    failures = []

    victim = Session(host, port, account, password, character)
    time.sleep(3)
    print(f"victim logged in: alive={victim.alive} bytes={victim.received}")
    if not victim.alive or victim.received < 1000:
        print("FAIL: the victim did not log in")
        return 2

    time.sleep(1.5)  # stay under the per-IP connection rate limit
    attacker = Session(host, port, "nosuchaccount", "wrongpassword", character)
    time.sleep(3)
    print(f"after a wrong-password login for the same character: victim alive={victim.alive}, attacker alive={attacker.alive}")
    if not victim.alive:
        failures.append("an unauthenticated login dropped the victim's client")
    if attacker.alive:
        failures.append("the unauthenticated connection was not closed")
    attacker.close()

    if victim.alive:
        time.sleep(1.5)
        owner = Session(host, port, account, password, character)
        time.sleep(3)
        print(f"after the owner's relogin: old connection alive={victim.alive}, new connection alive={owner.alive} bytes={owner.received}")
        if victim.alive:
            failures.append("the owner's relogin did not replace the old connection")
        if not owner.alive or owner.received < 1000:
            failures.append("the owner's relogin did not log in")
        owner.close()

    victim.close()
    for failure in failures:
        print(f"FAIL: {failure}")
    if not failures:
        print("OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
