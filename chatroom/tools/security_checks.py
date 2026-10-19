#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time
import uuid

from room_client import (
    CMD_CHAT_SEND,
    CMD_CHAT_SEND_RESP,
    CMD_ERROR,
    CMD_JOIN_ROOM,
    CMD_LOGIN,
    Client,
    enc,
    read_frame,
    u32,
)


def error_code(frame):
    if frame[3] != CMD_ERROR:
        return None
    return struct.unpack_from("!H", frame[5], 0)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--room-id", type=int, default=1)
    args = parser.parse_args()

    failures = 0
    try:
        # Login failures: 5 x 1004, then 1005 for the same IP + username.
        login_sock = Client(args.host, args.port)
        bad_user = "bad" + str(int(time.time())) + secrets.token_hex(3)
        codes = []
        for _ in range(6):
            login_sock.send(CMD_LOGIN, enc(bad_user) + enc("WrongPass123!"))
            frame = login_sock.recv()
            codes.append(error_code(frame))
        login_sock.close()
        print("login failure codes:", codes)
        if codes[:5] != [1004] * 5 or codes[5] != 1005:
            print("FAIL login rate limit")
            failures += 1
        else:
            print("PASS login rate limit")

        # Message rate limit: send 30 messages immediately, expect some 1005.
        suffix = str(int(time.time())) + secrets.token_hex(3)
        username = "rl" + suffix
        setup = Client(args.host, args.port)
        setup.register(username, "TestPass123!", "RateLimit")
        setup.login(username, "TestPass123!")
        setup.send(CMD_JOIN_ROOM, u32(args.room_id))
        setup.recv()
        for _ in range(30):
            setup.send(
                CMD_CHAT_SEND,
                u32(args.room_id) + enc(str(uuid.uuid4())) + enc("rate-limit-test"),
            )
        accepted = 0
        rejected = 0
        setup.sock.settimeout(0.5)
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            try:
                frame = setup.recv()
            except socket.timeout:
                break
            if frame[3] == CMD_CHAT_SEND_RESP:
                accepted += 1
            elif frame[3] == CMD_ERROR and error_code(frame) == 1005:
                rejected += 1
        setup.close()
        print("message rate: accepted=", accepted, "rejected=", rejected)
        if accepted == 0 or rejected == 0:
            print("FAIL message rate limit")
            failures += 1
        else:
            print("PASS message rate limit")

        return 0 if failures == 0 else 1
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
