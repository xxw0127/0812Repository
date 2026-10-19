#!/usr/bin/env python3
"""TLS smoke test for the public nginx endpoint."""
import argparse
import socket
import ssl
import sys

from room_client import (
    CMD_LOGIN,
    CMD_ROOM_LIST,
    FLAG_ERROR,
    enc,
    make_frame,
    read_frame,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.2.113")
    parser.add_argument("--port", type=int, default=9443)
    parser.add_argument("--ca", default="client/certs/ca.crt")
    parser.add_argument("--user", default="demo2")
    parser.add_argument("--password", default="Demo123456")
    args = parser.parse_args()

    context = ssl.create_default_context(cafile=args.ca)
    raw = socket.create_connection((args.host, args.port), timeout=8)
    sock = context.wrap_socket(raw, server_hostname=args.host)
    sock.settimeout(8)
    try:
        sock.sendall(make_frame(CMD_LOGIN, 1, enc(args.user) + enc(args.password)))
        frame = read_frame(sock)
        if frame[3] != CMD_LOGIN or frame[2] & FLAG_ERROR:
            print(f"FAIL login cmd={frame[3]} flags={frame[2]}")
            return 1

        sock.sendall(make_frame(CMD_ROOM_LIST, 2))
        frame = read_frame(sock)
        if frame[3] != CMD_ROOM_LIST or frame[2] & FLAG_ERROR:
            print(f"FAIL room list cmd={frame[3]} flags={frame[2]}")
            return 1
    finally:
        sock.close()

    print("PASS TLS login and room list after MySQL restart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
