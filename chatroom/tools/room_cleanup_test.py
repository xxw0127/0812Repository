#!/usr/bin/env python3
import argparse
import secrets
import struct
import sys
import time

from room_client import (
    CMD_JOIN_ROOM,
    CMD_ROOM_MEMBERS,
    Client,
    read_u32,
)


def member_count(client, room_id):
    client.send(CMD_ROOM_MEMBERS, struct.pack("!I", room_id))
    frame = client.recv()
    if frame[3] != CMD_ROOM_MEMBERS or frame[2] & 0x04:
        raise AssertionError(f"members failed: {frame}")
    _, offset = read_u32(frame[5], 0)
    return struct.unpack_from("!H", frame[5], offset)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--room-id", type=int, default=11)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    username = f"d{suffix}"
    password = "TestPass123!"

    setup = Client(args.host, args.port)
    setup.register(username, password, "Cleanup")
    setup.close()

    first = Client(args.host, args.port)
    first.login(username, password)
    first.send(CMD_JOIN_ROOM, struct.pack("!I", args.room_id))
    first.recv()
    first.close()
    time.sleep(0.5)

    second = Client(args.host, args.port)
    second.login(username, password)
    if member_count(second, args.room_id) != 0:
        raise AssertionError("disconnect did not remove room member")
    print("PASS disconnect cleanup")

    second.send(CMD_JOIN_ROOM, struct.pack("!I", args.room_id))
    second.recv()

    third = Client(args.host, args.port)
    third.login(username, password)
    time.sleep(0.5)
    if member_count(third, args.room_id) != 0:
        raise AssertionError("kick did not remove old room member")
    print("PASS kick cleanup")

    second.close()
    third.close()
    print("ALL CLEANUP CHECKS PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        sys.exit(1)
