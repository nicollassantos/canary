#!/usr/bin/env python3
"""Minimal Tibia 15.25 game client for load tests against a Canary server (standard library only).

It logs a character in on the current protocol and sends game packets; what the server sends back is
decrypted only to tell a successful login (0x17) from a refusal (0x14 with its message), then drained.

    python3 -I tools/loadtest/tibia_client.py HOST PORT EMAIL PASSWORD CHARACTER [--seconds N]

The server needs `authType = "password"` (the session key is "email\\npassword", as the client sends it
when the login server hands out the password). Only loopback hosts are accepted: this is a test tool
for one's own server, not something to point at other servers.

Wire format (src/server/network/protocol/transport_codec.cpp, protocolgame*.cpp):
- the server first sends a challenge: [u16 1][u32 adler][01][1F][u32 timestamp][u8 random][71];
- the first client message is unencrypted: [u16 blocks][u32 checksum][u8 pad][u8 0x0A][u16 os]
  [u16 version][u32 version][str][str][u8 preview][128 RSA bytes][...]; blocks * 8 + 4 is its size;
- afterwards each message is [u16 blocks][u32 sequence][XTEA([u8 padding][payload][padding])]; the
  client sequence starts at 1; bit 31 of the server's sequence marks a raw-deflate payload.
"""

import ipaddress
import os
import socket
import struct
import sys
import threading
import time
import zlib

# Standard OT RSA key (src/security/rsa.cpp default; the same modulus as key.pem).
P = 14299623962416399520070177382898895550795403345466153217470516082934737582776038882967213386204600674145392845853859217990626450972452084065728686565928113
Q = 7630979195970404721891201847792002125535401292779123937207447574596692788513647179235335529307251350570728407373705564708871762033017096809910315212884101
N = P * Q
E = 65537

CLIENT_VERSION = 1525
CLIENTOS_NEW_WINDOWS = 5
DELTA = 0x9E3779B9
MASK = 0xFFFFFFFF


def xtea_encrypt(data, key):
    out = bytearray()
    for offset in range(0, len(data), 8):
        v0, v1 = struct.unpack_from("<II", data, offset)
        total = 0
        for _ in range(32):
            v0 = (v0 + ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (total + key[total & 3]))) & MASK
            total = (total + DELTA) & MASK
            v1 = (v1 + ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (total + key[(total >> 11) & 3]))) & MASK
        out += struct.pack("<II", v0, v1)
    return bytes(out)


def xtea_decrypt(data, key):
    out = bytearray()
    for offset in range(0, len(data), 8):
        v0, v1 = struct.unpack_from("<II", data, offset)
        total = (DELTA * 32) & MASK
        for _ in range(32):
            v1 = (v1 - ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (total + key[(total >> 11) & 3]))) & MASK
            total = (total - DELTA) & MASK
            v0 = (v0 - ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (total + key[total & 3]))) & MASK
        out += struct.pack("<II", v0, v1)
    return bytes(out)


def string(text):
    raw = text.encode("latin-1")
    return struct.pack("<H", len(raw)) + raw


def position(x, y, z):
    return struct.pack("<HHB", x, y, z)


def require_loopback(host):
    address = ipaddress.ip_address(socket.gethostbyname(host))
    if not address.is_loopback:
        raise ValueError(f"{host} is not a loopback address: this tool only tests a local server")


class LoginRefused(Exception):
    pass


class GameConnection:
    """One logged-in character. Thread-safe `send`; a reader thread drains the server."""

    def __init__(self, host, port, email, password, character, source_ip=None, timeout=10.0):
        require_loopback(host)
        if source_ip is not None:
            require_loopback(source_ip)
        self.host, self.port = host, port
        self.email, self.password, self.character = email, password, character
        self.source_ip = source_ip
        self.timeout = timeout
        self.key = struct.unpack("<4I", os.urandom(16))
        self.sequence = 0
        self.send_lock = threading.Lock()
        self.sock = None
        self.closed = threading.Event()
        self.answered = threading.Event()  # the login got an answer, or the connection ended
        self.logged_in = False
        self.refusal = None
        self.bytes_received = 0
        self.packets_sent = 0
        # Frames to keep, decrypted, in `captured` (for checks that read an answer; empty means none kept):
        # those whose first message has one of `capture_opcodes`, and those containing one of the byte
        # strings in `capture_contains` (a frame often carries several messages, and only the first
        # one's opcode can be read without parsing them all).
        self.capture_opcodes = set()
        self.capture_contains = []
        self.captured = []

    # --- connection -------------------------------------------------------------------------------

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout,
                                             source_address=(self.source_ip, 0) if self.source_ip else None)
        timestamp, random = self._read_challenge()
        self.sock.sendall(self._first_message(timestamp, random))
        self.sock.settimeout(None)
        threading.Thread(target=self._read_loop, name=f"reader-{self.character}", daemon=True).start()
        if not self.answered.wait(self.timeout):
            self.close()
            raise LoginRefused(f"{self.character}: no answer to the login within {self.timeout}s")
        if not self.logged_in:
            self.close()
            raise LoginRefused(f"{self.character}: {self.refusal or 'connection closed'}")
        return self

    def close(self):
        self.closed.set()
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass

    def _recv_exact(self, size):
        data = b""
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise ConnectionError("closed by the server")
            data += chunk
        return data

    def _read_challenge(self):
        (blocks,) = struct.unpack("<H", self._recv_exact(2))
        body = self._recv_exact(blocks * 8 + 4)
        # [u32 adler][u8 padding][0x1F][u32 timestamp][u8 random][padding]
        if body[5] != 0x1F:
            raise LoginRefused(f"unexpected challenge opcode 0x{body[5]:02X}")
        return struct.unpack_from("<IB", body, 6)

    def _first_message(self, timestamp, random):
        session_key = f"{self.email}\n{self.password}"
        block = b"\x00" + struct.pack("<4I", *self.key) + b"\x00" + string(session_key) + string(self.character)
        block += struct.pack("<IB", timestamp, random)
        if len(block) > 128:
            raise ValueError("email, password and character name do not fit the RSA block")
        block += os.urandom(128 - len(block))
        encrypted = pow(int.from_bytes(block, "big"), E, N).to_bytes(128, "big")
        body = struct.pack("<HHI", CLIENTOS_NEW_WINDOWS, CLIENT_VERSION, CLIENT_VERSION)
        body += string("15.25.0") + string("loadtest") + b"\x00" + encrypted + b"\x00\x00"  # preview, RSA, OTCv8 probe (none)
        payload = b"\x00\x0A" + body
        payload += b"\x00" * ((-len(payload)) % 8)
        checksum = zlib.adler32(payload) & MASK
        return struct.pack("<HI", len(payload) // 8, checksum) + payload

    def _read_loop(self):
        first = True
        try:
            while not self.closed.is_set():
                (blocks,) = struct.unpack("<H", self._recv_exact(2))
                frame = self._recv_exact(blocks * 8 + 4)
                self.bytes_received += len(frame) + 2
                if first:
                    first = False
                    self._read_login_answer(frame)
                elif self.capture_opcodes or self.capture_contains:
                    payload = self._decode(frame)
                    if payload and (payload[0] in self.capture_opcodes
                                    or any(part in payload for part in self.capture_contains)):
                        self.captured.append(payload)
        except (OSError, ConnectionError, struct.error, zlib.error):
            pass
        finally:
            self.closed.set()
            self.answered.set()

    def _decode(self, frame):
        (sequence,) = struct.unpack_from("<I", frame, 0)
        plain = xtea_decrypt(frame[4:], self.key)
        padding = plain[0]
        payload = plain[1:len(plain) - padding]
        if sequence & 0x80000000:
            payload = zlib.decompressobj(-15).decompress(payload)
        return payload

    def _read_login_answer(self, frame):
        payload = self._decode(frame)
        if payload and payload[0] == 0x14:
            (length,) = struct.unpack_from("<H", payload, 1)
            self.refusal = payload[3:3 + length].decode("latin-1", "replace")
        elif payload and payload[0] == 0x16:
            self.refusal = "waiting list"
        else:
            self.logged_in = True
        self.answered.set()

    # --- sending ----------------------------------------------------------------------------------

    def send(self, payload):
        if self.closed.is_set():
            return False
        padding = (8 - (len(payload) + 1) % 8) % 8
        plain = bytes([padding]) + payload + b"\x00" * padding
        with self.send_lock:
            self.sequence = self.sequence + 1 if self.sequence < 0x7FFFFFFF else 1
            frame = struct.pack("<HI", len(plain) // 8, self.sequence) + xtea_encrypt(plain, self.key)
            try:
                self.sock.sendall(frame)
            except OSError:
                self.closed.set()
                return False
        self.packets_sent += 1
        return True

    # --- game packets (src/server/network/protocol/protocolgame.cpp, parse*) ----------------------

    def keepalive(self):
        return self.send(b"\x1E")  # ChatService::playerReceivePing: refreshes the server's lastPong

    def walk(self, direction):
        return self.send(bytes([{"n": 0x65, "e": 0x66, "s": 0x67, "w": 0x68, "ne": 0x6A, "se": 0x6B, "sw": 0x6C, "nw": 0x6D}[direction]]))

    def turn(self, direction):
        return self.send(bytes([{"n": 0x6F, "e": 0x70, "s": 0x71, "w": 0x72}[direction]]))

    def say(self, text):
        return self.send(b"\x96\x01" + string(text))  # TALKTYPE_SAY

    def typing(self, typing):
        return self.send(bytes([0x38, 1 if typing else 0]))

    def outfit_window(self):
        return self.send(b"\xD2")

    def use_item_with(self, from_pos, item_id, from_stack, to_pos, to_item_id, to_stack):
        return self.send(b"\x83" + position(*from_pos) + struct.pack("<HB", item_id, from_stack)
                         + position(*to_pos) + struct.pack("<HB", to_item_id, to_stack))

    def quick_loot_list(self, accepted, item_ids):
        return self.send(bytes([0x91, 1 if accepted else 0]) + struct.pack("<H", len(item_ids))
                         + b"".join(struct.pack("<H", i) for i in item_ids))

    def proficiency_window(self, weapon_id):
        return self.send(b"\xB3\x00" + struct.pack("<H", weapon_id))

    def proficiency_list(self):
        return self.send(b"\xB3\x01")

    def highscores(self, page=1, category=0, vocation=0xFFFFFFFF, world=""):
        # type 0 = HIGHSCORE_GETENTRIES; world category and BattlEye type are ignored by the server
        return self.send(b"\xB1\x00" + bytes([category]) + struct.pack("<I", vocation) + string(world)
                         + b"\x00\x00" + struct.pack("<H", page) + bytes([20]))

    def logout(self):
        return self.send(b"\x14")


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("email")
    parser.add_argument("password")
    parser.add_argument("character")
    parser.add_argument("--seconds", type=float, default=10.0, help="stay online this long, saying hello and walking")
    args = parser.parse_args()

    client = GameConnection(args.host, args.port, args.email, args.password, args.character).connect()
    print(f"logged in: {args.character}")
    deadline = time.monotonic() + args.seconds
    steps = ["n", "e", "s", "w"]
    step = 0
    client.say("hello from the load test")
    while time.monotonic() < deadline and not client.closed.is_set():
        client.keepalive()
        client.walk(steps[step % 4])
        step += 1
        time.sleep(1.0)
    alive = not client.closed.is_set()
    client.logout()
    time.sleep(0.5)
    client.close()
    print(f"online for {args.seconds:.0f}s: alive={alive} sent={client.packets_sent} received={client.bytes_received} bytes")
    return 0 if alive else 1


if __name__ == "__main__":
    sys.exit(main())
