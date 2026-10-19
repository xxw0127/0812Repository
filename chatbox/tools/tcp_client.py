#!/usr/bin/env python3
import argparse
import socket
import struct
import sys

MAGIC = 0x4348
VERSION = 1
HEADER = struct.Struct("!HBBHII")
CMD_PING = 0x0040
CMD_ERROR = 0x00FF


def make_frame(cmd: int, seq: int, body: bytes = b"", flags: int = 0) -> bytes:
    return HEADER.pack(MAGIC, VERSION, flags, cmd, seq, len(body)) + body


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed while reading")
        data.extend(chunk)
    return bytes(data)


def read_frame(sock: socket.socket):
    header = read_exact(sock, HEADER.size)
    magic, version, flags, cmd, seq, body_len = HEADER.unpack(header)
    body = read_exact(sock, body_len) if body_len else b""
    return magic, version, flags, cmd, seq, body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.2.113")
    parser.add_argument("--port", type=int, default=9100)
    args = parser.parse_args()

    failures = 0

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.settimeout(5)
        sock.sendall(make_frame(CMD_PING, 1, b"hello"))
        magic, version, flags, cmd, seq, body = read_frame(sock)
        if (magic, version, cmd, seq, body) != (MAGIC, 1, CMD_PING, 1, b"hello"):
            print("FAIL ping response mismatch", magic, version, flags, cmd, seq, body)
            failures += 1
        elif not (flags & 0x01):
            print("FAIL ping response missing response flag")
            failures += 1
        else:
            print("PASS ping round-trip")

        sock.sendall(make_frame(CMD_PING, 2, b"a") + make_frame(CMD_PING, 3, b"b"))
        first = read_frame(sock)
        second = read_frame(sock)
        if first[4] != 2 or first[5] != b"a" or second[4] != 3 or second[5] != b"b":
            print("FAIL pipelined ping ordering", first, second)
            failures += 1
        else:
            print("PASS pipelined ping ordering")

        sock.sendall(make_frame(0x7777, 4, b"x"))
        response = read_frame(sock)
        if response[3] != CMD_ERROR or not (response[2] & 0x04):
            print("FAIL unknown command response", response)
            failures += 1
        else:
            print("PASS unknown command error response")

    try:
        with socket.create_connection((args.host, args.port), timeout=5) as sock:
            sock.settimeout(5)
            bad = bytearray(make_frame(CMD_PING, 5, b"x"))
            bad[0] = 0
            bad[1] = 0
            sock.sendall(bad)
            closed = sock.recv(1) == b""
            if not closed:
                print("FAIL invalid magic did not close connection")
                failures += 1
            else:
                print("PASS invalid magic closes connection")
    except (ConnectionError, socket.timeout) as exc:
        print("PASS invalid magic closes connection (recv error:", exc, ")")

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
