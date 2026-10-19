#!/usr/bin/env python3
import argparse
import secrets
import struct
import sys
import time

from room_client import Client, enc, read_string, read_u64

CMD_USER_SEARCH = 0x0060
FLAG_ERROR = 0x04


def read_u8(body, offset):
    return body[offset], offset + 1


def search(client, keyword):
    client.send(CMD_USER_SEARCH, enc(keyword))
    frame = client.recv()
    if frame[3] != CMD_USER_SEARCH or frame[2] & FLAG_ERROR:
        raise AssertionError(f"user search failed: {frame}")
    count = struct.unpack_from("!H", frame[5], 0)[0]
    offset = 2
    rows = []
    for _ in range(count):
        user_id, offset = read_u64(frame[5], offset)
        username, offset = read_string(frame[5], offset)
        nickname, offset = read_string(frame[5], offset)
        online, offset = read_u8(frame[5], offset)
        rows.append((user_id, username, nickname, online))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9102)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"searcha{suffix}"
    user_b = f"searchb{suffix}"
    nick_b = f"SearchB{suffix}"
    password = "TestPass123!"

    setup = Client(args.host, args.port)
    a_id = setup.register(user_a, password, "Search A")
    b_id = setup.register(user_b, password, nick_b)
    setup.close()

    bob = Client(args.host, args.port)
    bob.login(user_b, password)
    time.sleep(0.2)

    alice = Client(args.host, args.port)
    alice.login(user_a, password)

    by_username = search(alice, user_b)
    if not any(row[0] == b_id and row[2] == nick_b and row[3] == 1
               for row in by_username):
        raise AssertionError(f"search by username failed: {by_username}")
    print("PASS search by username")

    by_nickname = search(alice, nick_b)
    if not any(row[0] == b_id for row in by_nickname):
        raise AssertionError(f"search by nickname failed: {by_nickname}")
    print("PASS search by nickname")

    if any(row[0] == a_id for row in search(alice, user_a)):
        raise AssertionError("search returned the caller")
    print("PASS search excludes caller")

    if search(alice, "%"):
        raise AssertionError("LIKE wildcard was not escaped")
    print("PASS search escapes LIKE wildcard")

    if search(alice, f"missing{suffix}"):
        raise AssertionError("unexpected search results")
    print("PASS empty search")

    alice.close()
    bob.close()
    print("ALL USER SEARCH CHECKS PASSED", a_id, b_id)
    return 0


if __name__ == "__main__":
    sys.exit(main())
