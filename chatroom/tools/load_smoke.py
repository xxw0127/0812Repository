#!/usr/bin/env python3
import argparse
import socket
import struct
import sys
import time

MAGIC = 0x4348
VERSION = 1
HEADER = struct.Struct("!HBBHII")
CMD_PING = 0x0040


def make_frame(seq: int) -> bytes:
    body = b"load"
    return HEADER.pack(MAGIC, VERSION, 0, CMD_PING, seq, len(body)) + body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.2.113")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--connections", type=int, default=500)
    args = parser.parse_args()

    sockets = []
    started = time.monotonic()
    try:
        for index in range(args.connections):
            sock = socket.create_connection((args.host, args.port), timeout=5)
            sock.settimeout(10)
            sockets.append(sock)

        for index, sock in enumerate(sockets):
            sock.sendall(make_frame(index + 1))

        failures = 0
        for index, sock in enumerate(sockets):
            try:
                header = b""
                while len(header) < HEADER.size:
                    chunk = sock.recv(HEADER.size - len(header))
                    if not chunk:
                        raise ConnectionError("closed")
                    header += chunk
                magic, version, flags, cmd, seq, body_len = HEADER.unpack(header)
                body = b""
                while len(body) < body_len:
                    chunk = sock.recv(body_len - len(body))
                    if not chunk:
                        raise ConnectionError("closed")
                    body += chunk
                if magic != MAGIC or cmd != CMD_PING or seq != index + 1 or body != b"load":
                    failures += 1
            except (ConnectionError, socket.timeout, OSError):
                failures += 1
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass

    elapsed = time.monotonic() - started
    print(
        f"connections={args.connections} failures={failures} "
        f"elapsed={elapsed:.2f}s"
    )
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
