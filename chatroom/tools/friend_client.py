#!/usr/bin/env python3
import argparse
import secrets
import struct
import sys
import time

from room_client import Client, enc, read_string, read_u64

CMD_FRIEND_REQUEST = 0x0090
CMD_FRIEND_RESPOND = 0x0091
CMD_FRIEND_LIST = 0x0092
CMD_FRIEND_REQUESTS = 0x0093
CMD_FRIEND_REMOVE = 0x0094
CMD_FRIEND_REQUEST_PUSH = 0x0095
CMD_FRIEND_ACCEPT_PUSH = 0x0096
CMD_FRIEND_SET_REMARK = 0x0097
CMD_USER_SEARCH = 0x0060
FLAG_ERROR = 0x04


def read_u8(body, offset):
    return body[offset], offset + 1


def read_u16(body, offset):
    return struct.unpack_from("!H", body, offset)[0], offset + 2


def read_u32(body, offset):
    return struct.unpack_from("!I", body, offset)[0], offset + 4


def friend_list(client):
    client.send(CMD_FRIEND_LIST)
    frame = client.recv()
    if frame[3] != CMD_FRIEND_LIST or frame[2] & FLAG_ERROR:
        raise AssertionError(f"friend list failed: {frame}")
    count, offset = read_u16(frame[5], 0)
    rows = []
    for _ in range(count):
        user_id, offset = read_u64(frame[5], offset)
        username, offset = read_string(frame[5], offset)
        nickname, offset = read_string(frame[5], offset)
        online, offset = read_u8(frame[5], offset)
        remark, offset = read_string(frame[5], offset)
        avatar_version, offset = read_u32(frame[5], offset)
        rows.append((user_id, username, nickname, online, remark, avatar_version))
    return rows


def friend_requests(client):
    client.send(CMD_FRIEND_REQUESTS)
    frame = client.recv()
    if frame[3] != CMD_FRIEND_REQUESTS or frame[2] & FLAG_ERROR:
        raise AssertionError(f"friend requests failed: {frame}")
    count, offset = read_u16(frame[5], 0)
    rows = []
    for _ in range(count):
        user_id, offset = read_u64(frame[5], offset)
        username, offset = read_string(frame[5], offset)
        nickname, offset = read_string(frame[5], offset)
        created_at, offset = read_string(frame[5], offset)
        rows.append((user_id, username, nickname, created_at))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9102)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    alice_user = f"fa{suffix}"
    bob_user = f"fb{suffix}"
    password = "TestPass123!"
    setup = Client(args.host, args.port)
    setup.register(alice_user, password, "Alice F")
    setup.register(bob_user, password, "Bob F")
    setup.close()

    alice = Client(args.host, args.port)
    bob = Client(args.host, args.port)
    a_id, _, _ = alice.login(alice_user, password)
    b_id, _, _ = bob.login(bob_user, password)

    alice.send(CMD_USER_SEARCH, enc(f"U{b_id:08d}"))
    response = alice.recv()
    if response[3] != CMD_USER_SEARCH or response[2] & FLAG_ERROR:
        raise AssertionError(f"public id search failed: {response}")
    count, offset = read_u16(response[5], 0)
    found_ids = []
    for _ in range(count):
        user_id, offset = read_u64(response[5], offset)
        _, offset = read_string(response[5], offset)
        _, offset = read_string(response[5], offset)
        _, offset = read_u8(response[5], offset)
        found_ids.append(user_id)
    if b_id not in found_ids:
        raise AssertionError(f"public id search missing user: {found_ids}")
    print("PASS public id search")

    alice.send(CMD_FRIEND_REQUEST, struct.pack("!Q", b_id))
    response = alice.recv()
    push = bob.recv()
    if response[3] != CMD_FRIEND_REQUEST or response[2] & FLAG_ERROR:
        raise AssertionError(f"friend request failed: {response}")
    if push[3] != CMD_FRIEND_REQUEST_PUSH:
        raise AssertionError(f"friend request push missing: {push}")
    from_id, _ = read_u64(push[5], 0)
    if from_id != a_id:
        raise AssertionError("friend request push sender mismatch")
    print("PASS friend request and push")

    pending = friend_requests(bob)
    if not any(row[0] == a_id for row in pending):
        raise AssertionError(f"pending request missing: {pending}")
    print("PASS friend request list")

    bob.send(CMD_FRIEND_RESPOND, struct.pack("!QB", a_id, 1))
    response = bob.recv()
    accept_push = alice.recv()
    if response[3] != CMD_FRIEND_RESPOND or response[2] & FLAG_ERROR:
        raise AssertionError(f"friend accept failed: {response}")
    if accept_push[3] != CMD_FRIEND_ACCEPT_PUSH:
        raise AssertionError(f"friend accept push missing: {accept_push}")
    print("PASS friend accept and push")

    alice_friends = friend_list(alice)
    bob_friends = friend_list(bob)
    if not any(row[0] == b_id for row in alice_friends):
        raise AssertionError("alice friend list missing bob")
    if not any(row[0] == a_id for row in bob_friends):
        raise AssertionError("bob friend list missing alice")
    for row in alice_friends + bob_friends:
        if row[5] != 0:
            raise AssertionError(f"new user avatar_version expected 0: {row}")
    print("PASS friend list both sides")

    alice.send(CMD_FRIEND_SET_REMARK, struct.pack("!Q", b_id) + enc("好友备注" * 8))
    response = alice.recv()
    if response[3] != CMD_FRIEND_SET_REMARK or response[2] & FLAG_ERROR:
        raise AssertionError(f"set remark failed: {response}")
    alice_rows = friend_list(alice)
    if not any(row[0] == b_id and row[4] == "好友备注" * 8 for row in alice_rows):
        raise AssertionError(f"remark missing on owner side: {alice_rows}")
    bob_rows = friend_list(bob)
    if not any(row[0] == a_id and row[4] == "" for row in bob_rows):
        raise AssertionError(f"remark leaked to friend side: {bob_rows}")
    print("PASS friend remark owner-only")

    alice.send(CMD_FRIEND_SET_REMARK, struct.pack("!Q", b_id) + enc(""))
    response = alice.recv()
    if response[3] != CMD_FRIEND_SET_REMARK or response[2] & FLAG_ERROR:
        raise AssertionError(f"clear remark failed: {response}")
    if not any(row[0] == b_id and row[4] == "" for row in friend_list(alice)):
        raise AssertionError("remark not cleared")
    print("PASS friend remark clear")

    alice.send(CMD_FRIEND_REMOVE, struct.pack("!Q", b_id))
    response = alice.recv()
    if response[3] != CMD_FRIEND_REMOVE or response[2] & FLAG_ERROR:
        raise AssertionError(f"friend remove failed: {response}")
    if any(row[0] == b_id for row in friend_list(alice)):
        raise AssertionError("friend still present after remove")
    print("PASS friend remove")

    alice.send(CMD_FRIEND_SET_REMARK, struct.pack("!Q", b_id) + enc("hidden"))
    response = alice.recv()
    if response[3] != 0x00FF or not (response[2] & FLAG_ERROR):
        raise AssertionError(f"remark on non-friend should fail: {response}")
    error_code, _ = read_u16(response[5], 0)
    if error_code != 1016:
        raise AssertionError(f"unexpected non-friend remark error: {error_code}")
    print("PASS friend remark non-friend rejected")

    alice.close()
    bob.close()
    print("ALL FRIEND CHECKS PASSED", a_id, b_id)
    return 0


if __name__ == "__main__":
    sys.exit(main())
