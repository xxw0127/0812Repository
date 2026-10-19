#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time
import uuid

from room_client import Client, FLAG_ERROR, enc, read_string, read_u64, u32

CMD_ROOM_LIST = 0x0010
CMD_JOIN_ROOM = 0x0011
CMD_ROOM_MEMBERS = 0x0013
CMD_CHAT_SEND = 0x0020
CMD_CHAT_SEND_RESP = 0x0021
CMD_CHAT_PUSH = 0x0022
CMD_HISTORY = 0x0050
CMD_HISTORY_RESP = 0x0051
CMD_FRIEND_REQUEST = 0x0090
CMD_FRIEND_RESPOND = 0x0091
CMD_FRIEND_LIST = 0x0092
CMD_PROFILE_GET = 0x00A0
CMD_PROFILE_UPDATE = 0x00A1
CMD_AVATAR_GET = 0x00A2
CMD_PROFILE_UPDATE_PUSH = 0x00A3
CMD_ERROR = 0x00FF


def read_u8(body, offset):
    return body[offset], offset + 1


def read_u16(body, offset):
    return struct.unpack_from("!H", body, offset)[0], offset + 2


def read_u32(body, offset):
    return struct.unpack_from("!I", body, offset)[0], offset + 4


def read_bytes(body, offset):
    length = struct.unpack_from("!H", body, offset)[0]
    offset += 2
    return body[offset : offset + length], offset + length


def expect_ok(frame, command):
    if frame[3] != command or frame[2] & FLAG_ERROR:
        raise AssertionError(f"unexpected response for 0x{command:04X}: {frame}")
    return frame[5]


def error_code(frame):
    if frame[3] != CMD_ERROR or not (frame[2] & FLAG_ERROR):
        raise AssertionError(f"expected an error frame, got: {frame}")
    code, _ = read_u16(frame[5], 0)
    return code


def profile_get(client, target_id):
    client.send(CMD_PROFILE_GET, struct.pack("!Q", target_id))
    body = expect_ok(client.recv(), CMD_PROFILE_GET)
    target, offset = read_u64(body, 0)
    username, offset = read_string(body, offset)
    nickname, offset = read_string(body, offset)
    signature, offset = read_string(body, offset)
    avatar_version, offset = read_u32(body, offset)
    avatar, offset = read_bytes(body, offset)
    online, offset = read_u8(body, offset)
    relationship, offset = read_u8(body, offset)
    remark, offset = read_string(body, offset)
    if offset != len(body):
        raise AssertionError(f"profile response has trailing bytes: {len(body) - offset}")
    return {
        "target_id": target,
        "username": username,
        "nickname": nickname,
        "signature": signature,
        "avatar_version": avatar_version,
        "avatar": avatar,
        "online": online,
        "relationship": relationship,
        "remark": remark,
    }


def avatar_get(client, target_id):
    client.send(CMD_AVATAR_GET, struct.pack("!Q", target_id))
    body = expect_ok(client.recv(), CMD_AVATAR_GET)
    target, offset = read_u64(body, 0)
    version, offset = read_u32(body, offset)
    avatar, offset = read_bytes(body, offset)
    if offset != len(body):
        raise AssertionError(f"avatar response has trailing bytes: {len(body) - offset}")
    return target, version, avatar


def build_update_body(nickname, signature, avatar_action, avatar, extra):
    body = enc(nickname) + enc(signature) + struct.pack("!B", avatar_action)
    if avatar_action == 1:
        body += struct.pack("!H", len(avatar)) + avatar
    return body + extra


def profile_update(client, nickname, signature, avatar_action=0, avatar=b"", extra=b""):
    client.send(
        CMD_PROFILE_UPDATE,
        build_update_body(nickname, signature, avatar_action, avatar, extra),
    )
    body = expect_ok(client.recv(), CMD_PROFILE_UPDATE)
    returned_nickname, offset = read_string(body, 0)
    returned_signature, offset = read_string(body, offset)
    version, offset = read_u32(body, offset)
    if offset != len(body):
        raise AssertionError(f"update response has trailing bytes: {len(body) - offset}")
    return returned_nickname, returned_signature, version


def expect_update_error(client, nickname, signature, avatar_action=0, avatar=b"", extra=b"",
                        expected=1001):
    client.send(
        CMD_PROFILE_UPDATE,
        build_update_body(nickname, signature, avatar_action, avatar, extra),
    )
    code = error_code(client.recv())
    if code != expected:
        raise AssertionError(f"expected update error {expected}, got {code}")


def profile_push(client):
    frame = client.recv()
    if frame[3] != CMD_PROFILE_UPDATE_PUSH:
        raise AssertionError(f"expected profile update push, got: {frame}")
    body = frame[5]
    user_id, offset = read_u64(body, 0)
    nickname, offset = read_string(body, offset)
    version, offset = read_u32(body, offset)
    if offset != len(body):
        raise AssertionError(f"profile push has trailing bytes: {len(body) - offset}")
    return user_id, nickname, version


def expect_no_frame(client, seconds=0.5):
    previous = client.sock.gettimeout()
    client.sock.settimeout(seconds)
    try:
        frame = client.recv()
        raise AssertionError(f"unexpected frame: {frame}")
    except socket.timeout:
        pass
    finally:
        client.sock.settimeout(previous)


def make_friends(requester, target, requester_id, target_id):
    requester.send(CMD_FRIEND_REQUEST, struct.pack("!Q", target_id))
    response = requester.recv()
    if response[3] != CMD_FRIEND_REQUEST or response[2] & FLAG_ERROR:
        raise AssertionError(f"friend request failed: {response}")
    push = target.recv()
    if push[3] != 0x0095:
        raise AssertionError(f"friend request push missing: {push}")

    target.send(CMD_FRIEND_RESPOND, struct.pack("!QB", requester_id, 1))
    response = target.recv()
    if response[3] != CMD_FRIEND_RESPOND or response[2] & FLAG_ERROR:
        raise AssertionError(f"friend accept failed: {response}")
    push = requester.recv()
    if push[3] != 0x0096:
        raise AssertionError(f"friend accept push missing: {push}")


def friend_list(client):
    client.send(CMD_FRIEND_LIST)
    body = expect_ok(client.recv(), CMD_FRIEND_LIST)
    count, offset = read_u16(body, 0)
    rows = []
    for _ in range(count):
        user_id, offset = read_u64(body, offset)
        username, offset = read_string(body, offset)
        nickname, offset = read_string(body, offset)
        online, offset = read_u8(body, offset)
        remark, offset = read_string(body, offset)
        avatar_version, offset = read_u32(body, offset)
        rows.append((user_id, username, nickname, online, remark, avatar_version))
    if offset != len(body):
        raise AssertionError(f"friend list has trailing bytes: {len(body) - offset}")
    return rows


def first_room(client):
    client.send(CMD_ROOM_LIST)
    body = expect_ok(client.recv(), CMD_ROOM_LIST)
    count, offset = read_u16(body, 0)
    for _ in range(count):
        room_id, offset = read_u32(body, offset)
        _, offset = read_string(body, offset)
        max_members, offset = read_u32(body, offset)
        _, offset = read_u32(body, offset)
        if max_members >= 2:
            return room_id
    raise AssertionError("no usable room")


def join_room(client, room_id):
    client.send(CMD_JOIN_ROOM, u32(room_id))
    response = client.recv()
    if response[3] != CMD_JOIN_ROOM or response[2] & FLAG_ERROR:
        raise AssertionError(f"join room failed: {response}")


def room_members(client, room_id):
    client.send(CMD_ROOM_MEMBERS, u32(room_id))
    body = expect_ok(client.recv(), CMD_ROOM_MEMBERS)
    _, offset = read_u32(body, 0)
    count, offset = read_u16(body, offset)
    members = []
    for _ in range(count):
        user_id, offset = read_u64(body, offset)
        nickname, offset = read_string(body, offset)
        members.append((user_id, nickname))
    return members


def room_history(client, room_id, limit=20):
    client.send(CMD_HISTORY, struct.pack("!BQQH", 1, room_id, 0, limit))
    body = expect_ok(client.recv(), CMD_HISTORY_RESP)
    kind, offset = read_u8(body, 0)
    target, offset = read_u64(body, offset)
    count, offset = read_u16(body, offset)
    _, offset = read_u8(body, offset)
    _, offset = read_u64(body, offset)
    messages = []
    for _ in range(count):
        message_id, offset = read_u64(body, offset)
        sender_id, offset = read_u64(body, offset)
        nickname, offset = read_string(body, offset)
        content, offset = read_string(body, offset)
        created_at, offset = read_string(body, offset)
        reply_to, offset = read_u64(body, offset)
        reply_sender, offset = read_string(body, offset)
        reply_content, offset = read_string(body, offset)
        recalled, offset = read_u8(body, offset)
        messages.append(
            (message_id, sender_id, nickname, content, created_at, reply_to,
             reply_sender, reply_content, recalled)
        )
    if kind != 1 or target != room_id:
        raise AssertionError(f"history target mismatch: {kind} {target} {room_id}")
    return messages


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9102)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    alice_user = f"pa{suffix}"
    bob_user = f"pb{suffix}"
    carol_user = f"pc{suffix}"
    password = "TestPass123!"
    bob_nickname = "Bob Profile"
    carol_nickname = "Carol Profile"

    setup = Client(args.host, args.port)
    setup.register(alice_user, password, "Alice Profile")
    setup.register(bob_user, password, bob_nickname)
    setup.register(carol_user, password, carol_nickname)
    setup.close()

    alice = Client(args.host, args.port)
    bob = Client(args.host, args.port)
    carol = Client(args.host, args.port)
    a_id, a_nick, _ = alice.login(alice_user, password)
    b_id, b_nick, _ = bob.login(bob_user, password)
    c_id, c_nick, _ = carol.login(carol_user, password)
    if a_nick != "Alice Profile" or b_nick != bob_nickname or c_nick != carol_nickname:
        raise AssertionError(f"login nickname mismatch: {a_nick!r} {b_nick!r} {c_nick!r}")

    profile = profile_get(alice, b_id)
    if profile["target_id"] != b_id:
        raise AssertionError(f"uid mismatch: {profile['target_id']} != {b_id}")
    if profile["username"] != bob_user:
        raise AssertionError(f"username mismatch: {profile['username']!r}")
    if profile["nickname"] != bob_nickname:
        raise AssertionError(f"nickname mismatch: {profile['nickname']!r}")
    if profile["signature"] != "":
        raise AssertionError(f"signature should be empty: {profile['signature']!r}")
    if profile["avatar_version"] != 0:
        raise AssertionError(f"avatar version should be 0: {profile['avatar_version']}")
    if profile["avatar"] != b"":
        raise AssertionError(f"avatar should be empty: {profile['avatar']!r}")
    if profile["online"] != 1:
        raise AssertionError(f"target should be online: {profile['online']}")
    if profile["relationship"] != 2:
        raise AssertionError(f"non-friend relation should be 2: {profile['relationship']}")
    if profile["remark"] != "":
        raise AssertionError(f"non-friend remark should be empty: {profile['remark']!r}")
    print("PASS profile get non-friend", b_id)

    target_id, version, avatar = avatar_get(alice, b_id)
    if target_id != b_id:
        raise AssertionError(f"avatar uid mismatch: {target_id} != {b_id}")
    if version != 0:
        raise AssertionError(f"avatar version should be 0: {version}")
    if avatar != b"":
        raise AssertionError(f"avatar should be empty: {avatar!r}")
    print("PASS avatar get empty", version)

    missing_id = (1 << 64) - 1
    alice.send(CMD_PROFILE_GET, struct.pack("!Q", missing_id))
    code = error_code(alice.recv())
    if code != 1008:
        raise AssertionError(f"missing profile should return 1008, got {code}")
    print("PASS profile get missing user 1008")

    make_friends(alice, bob, a_id, b_id)
    make_friends(alice, carol, a_id, c_id)
    print("PASS friend setup")

    # Nickname/signature update; both online friends receive one push each.
    updated_nickname = "Alice Updated"
    returned_nickname, returned_signature, returned_version = profile_update(
        alice, f"  {updated_nickname}  ", "  hello world  ", 0
    )
    if returned_nickname != updated_nickname:
        raise AssertionError(f"update nickname mismatch: {returned_nickname!r}")
    if returned_signature != "hello world":
        raise AssertionError(f"update signature mismatch: {returned_signature!r}")
    if returned_version != 0:
        raise AssertionError(f"update version should be 0: {returned_version}")

    pushed_user, pushed_nickname, pushed_version = profile_push(bob)
    if (pushed_user, pushed_nickname, pushed_version) != (
        a_id, updated_nickname, 0
    ):
        raise AssertionError(
            f"bob push mismatch: {pushed_user} {pushed_nickname!r} {pushed_version}"
        )
    pushed_user, pushed_nickname, pushed_version = profile_push(carol)
    if (pushed_user, pushed_nickname, pushed_version) != (
        a_id, updated_nickname, 0
    ):
        raise AssertionError(
            f"carol push mismatch: {pushed_user} {pushed_nickname!r} {pushed_version}"
        )

    profile = profile_get(bob, a_id)
    if profile["nickname"] != updated_nickname:
        raise AssertionError(f"updated nickname not visible: {profile['nickname']!r}")
    if profile["signature"] != "hello world":
        raise AssertionError(f"updated signature not visible: {profile['signature']!r}")
    if profile["username"] != alice_user:
        raise AssertionError(f"username changed: {profile['username']!r}")
    if profile["relationship"] != 1:
        raise AssertionError(f"friend relation should be 1: {profile['relationship']}")

    own = profile_get(alice, a_id)
    if own["username"] != alice_user or own["nickname"] != updated_nickname:
        raise AssertionError(f"own profile mismatch: {own}")
    if own["relationship"] != 0:
        raise AssertionError(f"self relation should be 0: {own['relationship']}")
    print("PASS profile update nickname/signature and push")

    # Username is not an editable field and a foreign target cannot be updated.
    expect_update_error(
        alice, updated_nickname, "hello world", 0,
        extra=struct.pack("!Q", b_id) + enc("HackedNick"), expected=1001,
    )
    bob_profile = profile_get(alice, b_id)
    if bob_profile["username"] != bob_user or bob_profile["nickname"] != bob_nickname:
        raise AssertionError(f"another user's profile changed: {bob_profile}")
    print("PASS username/target fields rejected")

    # Invalid nickname/signature/action inputs.
    expect_update_error(alice, "   ", "hello world", 0, expected=1001)
    expect_update_error(alice, "超" * 33, "hello world", 0, expected=1001)
    expect_update_error(alice, "bad\x01name", "hello world", 0, expected=1001)
    expect_update_error(alice, updated_nickname, "签" * 129, 0, expected=1001)
    expect_update_error(alice, updated_nickname, "bad\x1fsignature", 0, expected=1001)
    expect_update_error(alice, updated_nickname, "hello world", 3, expected=1001)
    after_invalid = profile_get(alice, a_id)
    if (after_invalid["nickname"], after_invalid["signature"]) != (
        updated_nickname, "hello world"
    ):
        raise AssertionError(f"invalid update changed profile: {after_invalid}")
    expect_no_frame(bob, 0.4)
    print("PASS invalid nickname/signature/action rejected")

    # Signature-only update: 128 code points accepted, no push.
    signature_128 = "签" * 128
    returned_nickname, returned_signature, returned_version = profile_update(
        alice, updated_nickname, signature_128, 0
    )
    if returned_signature != signature_128 or returned_version != 0:
        raise AssertionError(
            f"128-code-point signature mismatch: {returned_signature!r} {returned_version}"
        )
    expect_no_frame(bob, 0.5)
    profile = profile_get(alice, a_id)
    if profile["signature"] != signature_128:
        raise AssertionError("128-code-point signature not persisted")
    print("PASS signature-only update has no push")

    # 32 Chinese characters must survive every read path.
    nickname_32 = "汉" * 32
    returned_nickname, returned_signature, returned_version = profile_update(
        alice, nickname_32, "room signature", 0
    )
    if returned_nickname != nickname_32:
        raise AssertionError(f"32-char nickname mismatch: {returned_nickname!r}")
    pushed_user, pushed_nickname, pushed_version = profile_push(bob)
    if (pushed_user, pushed_nickname, pushed_version) != (a_id, nickname_32, 0):
        raise AssertionError(
            f"32-char push mismatch: {pushed_user} {pushed_nickname!r} {pushed_version}"
        )

    profile = profile_get(bob, a_id)
    if profile["nickname"] != nickname_32:
        raise AssertionError(f"32-char nickname not in profile: {profile['nickname']!r}")
    rows = friend_list(bob)
    expected_rows = [(a_id, alice_user, nickname_32, 1, "", 0)]
    if rows != expected_rows:
        raise AssertionError(
            f"friend list layout mismatch: {rows} != {expected_rows}"
        )

    room_id = first_room(alice)
    join_room(alice, room_id)
    join_room(bob, room_id)
    members = room_members(bob, room_id)
    if not any(member[0] == a_id and member[1] == nickname_32 for member in members):
        raise AssertionError(f"32-char nickname missing from room members: {members}")

    content = f"nickname propagation {uuid.uuid4()}"
    alice.send(CMD_CHAT_SEND, u32(room_id) + enc(str(uuid.uuid4())) + enc(content))
    alice_frames = [alice.recv(), alice.recv()]
    if not any(frame[3] == CMD_CHAT_SEND_RESP for frame in alice_frames):
        raise AssertionError(f"chat response missing: {alice_frames}")
    if not any(frame[3] == CMD_CHAT_PUSH for frame in alice_frames):
        raise AssertionError(f"own chat push missing: {alice_frames}")
    chat_push = bob.recv()
    if chat_push[3] != CMD_CHAT_PUSH:
        raise AssertionError(f"chat push missing: {chat_push}")
    _, offset = read_u32(chat_push[5], 0)
    _, offset = read_u64(chat_push[5], offset)
    sender_id, offset = read_u64(chat_push[5], offset)
    sender_nickname, offset = read_string(chat_push[5], offset)
    pushed_content, offset = read_string(chat_push[5], offset)
    if sender_id != a_id or sender_nickname != nickname_32 or pushed_content != content:
        raise AssertionError(
            f"chat push nickname mismatch: {sender_id} {sender_nickname!r} {pushed_content!r}"
        )

    messages = room_history(alice, room_id)
    matching = [message for message in messages if message[3] == content]
    if not matching or matching[0][2] != nickname_32:
        raise AssertionError(f"history nickname mismatch: {matching}")
    print("PASS 32-Chinese-character nickname propagation", room_id)

    # Avatar set/clear and version increments.
    jpeg = b"\xff\xd8\xff\xe0" + bytes(range(64)) * 4
    returned_nickname, returned_signature, returned_version = profile_update(
        alice, nickname_32, "room signature", 1, jpeg
    )
    if returned_version != 1:
        raise AssertionError(f"avatar set version should be 1: {returned_version}")
    pushed_user, pushed_nickname, pushed_version = profile_push(bob)
    if (pushed_user, pushed_nickname, pushed_version) != (a_id, nickname_32, 1):
        raise AssertionError(
            f"avatar set push mismatch: {pushed_user} {pushed_nickname!r} {pushed_version}"
        )
    target, version, stored = avatar_get(bob, a_id)
    if target != a_id or version != 1 or stored != jpeg:
        raise AssertionError(
            f"avatar set mismatch: {target} {version} {len(stored)} != {len(jpeg)}"
        )
    profile = profile_get(bob, a_id)
    if profile["avatar_version"] != 1 or profile["avatar"] != jpeg:
        raise AssertionError("profile GET did not return uploaded avatar")
    print("PASS avatar upload and version 1")

    returned_nickname, returned_signature, returned_version = profile_update(
        alice, nickname_32, "room signature", 2
    )
    if returned_version != 2:
        raise AssertionError(f"avatar clear version should be 2: {returned_version}")
    pushed_user, pushed_nickname, pushed_version = profile_push(bob)
    if (pushed_user, pushed_nickname, pushed_version) != (a_id, nickname_32, 2):
        raise AssertionError(
            f"avatar clear push mismatch: {pushed_user} {pushed_nickname!r} {pushed_version}"
        )
    target, version, stored = avatar_get(bob, a_id)
    if target != a_id or version != 2 or stored != b"":
        raise AssertionError(f"avatar clear mismatch: {target} {version} {stored!r}")
    profile = profile_get(bob, a_id)
    if profile["avatar_version"] != 2 or profile["avatar"] != b"":
        raise AssertionError("profile GET still has avatar after clear")
    print("PASS avatar clear and version 2")

    expect_update_error(
        alice, nickname_32, "room signature", 1, b"GIF89a" + b"\x00" * 32, expected=1017
    )
    too_large = b"\xff\xd8\xff" + b"\x00" * (48 * 1024)
    expect_update_error(
        alice, nickname_32, "room signature", 1, too_large, expected=1017
    )
    _, version, stored = avatar_get(alice, a_id)
    if version != 2 or stored != b"":
        raise AssertionError(f"invalid avatar changed version: {version}")
    expect_no_frame(bob, 0.5)
    print("PASS invalid and oversized avatars return 1017")

    # Offline friend must not receive the push; online friend must.
    final_nickname = "Offline Friend Check"
    carol.close()
    time.sleep(0.3)
    returned_nickname, returned_signature, returned_version = profile_update(
        alice, final_nickname, "final signature", 0
    )
    if returned_nickname != final_nickname:
        raise AssertionError(f"final nickname mismatch: {returned_nickname!r}")
    pushed_user, pushed_nickname, pushed_version = profile_push(bob)
    if (pushed_user, pushed_nickname, pushed_version) != (
        a_id, final_nickname, 2
    ):
        raise AssertionError(
            f"online friend push mismatch: {pushed_user} {pushed_nickname!r} {pushed_version}"
        )

    carol = Client(args.host, args.port)
    carol.login(carol_user, password)
    expect_no_frame(carol, 0.6)
    profile = profile_get(carol, a_id)
    if profile["nickname"] != final_nickname:
        raise AssertionError(f"offline friend missed stored update: {profile['nickname']!r}")
    print("PASS online friend push, offline friend no push")

    alice.close()
    bob.close()
    carol.close()
    print("ALL PROFILE CHECKS PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        sys.exit(1)
