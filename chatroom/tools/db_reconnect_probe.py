#!/usr/bin/env python3
"""Probe used by the MySQL reconnect test.

Run once to create the probe account, kill the test DB connections, then run
again. The old server keeps a dead MYSQL* and returns 2001 forever; the
reconnecting server pings, reconnects, and logs in normally.
"""
import argparse
import struct
import sys

from room_client import (
    Client,
    CMD_ERROR,
    CMD_LOGIN,
    CMD_REGISTER,
    CMD_ROOM_LIST,
    FLAG_ERROR,
    enc,
    read_string,
)

PASSWORD = "Reconnect123!"
USERNAME = "reconnect_probe"


def error_code(frame):
    if frame[3] != CMD_ERROR or not (frame[2] & FLAG_ERROR) or len(frame[5]) < 2:
        return None
    return struct.unpack_from("!H", frame[5], 0)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9102)
    args = parser.parse_args()

    client = Client(args.host, args.port)
    try:
        client.send(CMD_REGISTER, enc(USERNAME) + enc(PASSWORD) + enc("Reconnect Probe"))
        frame = client.recv()
        if frame[3] == CMD_ERROR:
            code = error_code(frame)
            if code != 1003:
                message, _ = read_string(frame[5], 2) if len(frame[5]) >= 2 else ("", 0)
                print(f"FAIL register code={code} message={message}")
                return 1
        elif frame[3] != CMD_REGISTER or frame[2] & FLAG_ERROR:
            print(f"FAIL unexpected register response cmd={frame[3]}")
            return 1

        client.send(CMD_LOGIN, enc(USERNAME) + enc(PASSWORD))
        frame = client.recv()
        if frame[3] != CMD_LOGIN or frame[2] & FLAG_ERROR:
            code = error_code(frame)
            message, _ = read_string(frame[5], 2) if len(frame[5]) >= 2 else ("", 0)
            print(f"FAIL login code={code} message={message}")
            return 1

        client.send(CMD_ROOM_LIST)
        frame = client.recv()
        if frame[3] != CMD_ROOM_LIST or frame[2] & FLAG_ERROR:
            print(f"FAIL room list cmd={frame[3]} flags={frame[2]}")
            return 1
    finally:
        client.close()

    print("PASS reconnect probe login and room list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
