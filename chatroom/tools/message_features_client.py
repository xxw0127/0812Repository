#!/usr/bin/env python3
import argparse
import secrets
import struct
import sys
import time
import uuid

from room_client import (
    CMD_CHAT_PUSH,
    CMD_CHAT_SEND,
    CMD_CHAT_SEND_RESP,
    CMD_JOIN_ROOM,
    Client,
    enc,
    read_string,
    read_u32,
    read_u64,
    u32,
)

CMD_PRIVATE_SEND = 0x0030
CMD_PRIVATE_SEND_RESP = 0x0031
CMD_PRIVATE_PUSH = 0x0032
CMD_PROFILE_UPDATE = 0x00A1
CMD_PROFILE_UPDATE_PUSH = 0x00A3
CMD_MESSAGE_RECALL = 0x0070
CMD_MESSAGE_SEARCH = 0x0080
FLAG_ERROR = 0x04


def read_u8(body, offset):
    return body[offset], offset + 1


def read_u16(body, offset):
    return struct.unpack_from("!H", body, offset)[0], offset + 2


def send_room(client, room_id, content, client_id, reply_to=0, reply_sender="",
              reply_content=""):
    body = u32(room_id) + enc(client_id) + enc(content)
    if reply_to:
        body += struct.pack("!Q", reply_to) + enc(reply_sender) + enc(reply_content)
    client.send(CMD_CHAT_SEND, body)
    while True:
        response = client.recv()
        if response[3] == CMD_CHAT_PUSH:
            continue
        break
    if response[3] != CMD_CHAT_SEND_RESP or response[2] & FLAG_ERROR:
        raise AssertionError(f"room send failed: {response}")
    message_id, offset = read_u64(response[5], 0)
    created_at, offset = read_string(response[5], offset)
    return message_id, created_at


def send_private(client, peer_id, content, client_id, reply_to=0, reply_sender="",
                 reply_content=""):
    body = struct.pack("!Q", peer_id) + enc(client_id) + enc(content)
    if reply_to:
        body += struct.pack("!Q", reply_to) + enc(reply_sender) + enc(reply_content)
    client.send(CMD_PRIVATE_SEND, body)
    response = client.recv()
    if response[3] != CMD_PRIVATE_SEND_RESP or response[2] & FLAG_ERROR:
        raise AssertionError(f"private send failed: {response}")
    message_id, offset = read_u64(response[5], 0)
    created_at, offset = read_string(response[5], offset)
    return message_id, created_at


def parse_push_reply(body, offset):
    if len(body) - offset < 8:
        return 0, "", "", offset
    reply_to, offset = read_u64(body, offset)
    reply_sender, offset = read_string(body, offset)
    reply_content, offset = read_string(body, offset)
    return reply_to, reply_sender, reply_content, offset


def parse_room_push(frame):
    body = frame[5]
    room_id, offset = read_u32(body, 0)
    message_id, offset = read_u64(body, offset)
    sender_id, offset = read_u64(body, offset)
    nickname, offset = read_string(body, offset)
    content, offset = read_string(body, offset)
    created_at, offset = read_string(body, offset)
    reply_to, reply_sender, reply_content, offset = parse_push_reply(body, offset)
    if offset != len(body):
        raise AssertionError(f"room push has trailing bytes: {len(body) - offset}")
    return {
        "room_id": room_id,
        "message_id": message_id,
        "sender_id": sender_id,
        "nickname": nickname,
        "content": content,
        "created_at": created_at,
        "reply_to": reply_to,
        "reply_sender": reply_sender,
        "reply_content": reply_content,
    }


def parse_private_push(frame):
    body = frame[5]
    sender_id, offset = read_u64(body, 0)
    message_id, offset = read_u64(body, offset)
    nickname, offset = read_string(body, offset)
    content, offset = read_string(body, offset)
    created_at, offset = read_string(body, offset)
    reply_to, reply_sender, reply_content, offset = parse_push_reply(body, offset)
    if offset != len(body):
        raise AssertionError(f"private push has trailing bytes: {len(body) - offset}")
    return {
        "sender_id": sender_id,
        "message_id": message_id,
        "nickname": nickname,
        "content": content,
        "created_at": created_at,
        "reply_to": reply_to,
        "reply_sender": reply_sender,
        "reply_content": reply_content,
    }


def recv_push_matching(client, command, predicate):
    while True:
        frame = client.recv()
        if frame[3] != command:
            continue
        parsed = parse_room_push(frame) if command == CMD_CHAT_PUSH else parse_private_push(frame)
        if predicate(parsed):
            return parsed


def profile_update_nickname(client, nickname):
    client.send(CMD_PROFILE_UPDATE, enc(nickname) + enc("") + struct.pack("!B", 0))
    while True:
        frame = client.recv()
        if frame[3] == CMD_PROFILE_UPDATE_PUSH:
            continue
        break
    if frame[3] != CMD_PROFILE_UPDATE or frame[2] & FLAG_ERROR:
        raise AssertionError(f"profile update failed: {frame}")
    returned, offset = read_string(frame[5], 0)
    if returned != nickname:
        raise AssertionError(f"profile update nickname mismatch: {returned!r}")
    return returned


def exercise_long_nickname_replies(alice, bob, room_id, a_id, b_id):
    cases = [("32-Chinese-character", "汉" * 32), ("32-emoji", "😀" * 32)]
    for label, nickname in cases:
        profile_update_nickname(bob, nickname)
        if len(nickname.encode("utf-8")) <= 32:
            raise AssertionError(f"{label} nickname must exceed 32 bytes")
        if len(nickname) != 32:
            raise AssertionError(f"{label} nickname must be 32 code points")

        room_original = f"room original {label} {uuid.uuid4()}"
        room_original_id, _ = send_room(
            bob, room_id, room_original, str(uuid.uuid4()))
        original_push = recv_push_matching(
            alice, CMD_CHAT_PUSH,
            lambda parsed, message_id=room_original_id: parsed["message_id"] == message_id)
        if original_push["nickname"] != nickname:
            raise AssertionError(
                f"room original sender nickname mismatch for {label}: "
                f"{original_push['nickname']!r}")

        room_quote = f"room quote {label} {uuid.uuid4()}"
        send_room(
            alice, room_id, room_quote, str(uuid.uuid4()), room_original_id,
            nickname, room_original)
        quote_push = recv_push_matching(
            bob, CMD_CHAT_PUSH,
            lambda parsed, message_id=room_original_id: parsed["reply_to"] == message_id)
        if quote_push["reply_sender"] != nickname:
            raise AssertionError(
                f"room reply sender mismatch for {label}: "
                f"{quote_push['reply_sender']!r}")
        if quote_push["reply_content"] != room_original:
            raise AssertionError(f"room reply content mismatch for {label}")
        print(f"PASS room reply with {label} nickname ({len(nickname.encode('utf-8'))} bytes)")

        private_original = f"private original {label} {uuid.uuid4()}"
        private_original_id, _ = send_private(
            bob, a_id, private_original, str(uuid.uuid4()))
        original_push = recv_push_matching(
            alice, CMD_PRIVATE_PUSH,
            lambda parsed, message_id=private_original_id: parsed["message_id"] == message_id)
        if original_push["nickname"] != nickname:
            raise AssertionError(
                f"private original sender nickname mismatch for {label}: "
                f"{original_push['nickname']!r}")

        private_quote = f"private quote {label} {uuid.uuid4()}"
        send_private(
            alice, b_id, private_quote, str(uuid.uuid4()), private_original_id,
            nickname, private_original)
        quote_push = recv_push_matching(
            bob, CMD_PRIVATE_PUSH,
            lambda parsed, message_id=private_original_id: parsed["reply_to"] == message_id)
        if quote_push["reply_sender"] != nickname:
            raise AssertionError(
                f"private reply sender mismatch for {label}: "
                f"{quote_push['reply_sender']!r}")
        if quote_push["reply_content"] != private_original:
            raise AssertionError(f"private reply content mismatch for {label}")
        print(f"PASS private reply with {label} nickname ({len(nickname.encode('utf-8'))} bytes)")


def search(client, kind, target, keyword, limit=20):
    client.send(CMD_MESSAGE_SEARCH,
                struct.pack("!BQ", kind, target) + enc(keyword) + struct.pack("!H", limit))
    frame = client.recv()
    if frame[3] != CMD_MESSAGE_SEARCH or frame[2] & FLAG_ERROR:
        raise AssertionError(f"search failed: {frame}")
    count, offset = read_u16(frame[5], 0)
    rows = []
    for _ in range(count):
        message_id, offset = read_u64(frame[5], offset)
        sender_id, offset = read_u64(frame[5], offset)
        nickname, offset = read_string(frame[5], offset)
        content, offset = read_string(frame[5], offset)
        created_at, offset = read_string(frame[5], offset)
        recalled, offset = read_u8(frame[5], offset)
        rows.append((message_id, sender_id, nickname, content, created_at, recalled))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9102)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    alice_user = f"m{suffix}a"
    bob_user = f"m{suffix}b"
    password = "TestPass123!"
    setup = Client(args.host, args.port)
    setup.register(alice_user, password, "Alice")
    setup.register(bob_user, password, "Bob")
    setup.close()

    alice = Client(args.host, args.port)
    bob = Client(args.host, args.port)
    a_id, _, _ = alice.login(alice_user, password)
    b_id, _, _ = bob.login(bob_user, password)

    alice.send(CMD_JOIN_ROOM, u32(1))
    alice.recv()
    bob.send(CMD_JOIN_ROOM, u32(1))
    bob.recv()

    first_id, _ = send_room(alice, 1, "original room text", str(uuid.uuid4()))
    bob_push = bob.recv()
    if bob_push[3] != CMD_CHAT_PUSH:
        raise AssertionError("room push missing")

    quoted_id, _ = send_room(
        alice, 1, "quoted room text", str(uuid.uuid4()), first_id, "Alice",
        "original room text")
    bob_push = bob.recv()
    room_id, offset = read_u32(bob_push[5], 0)
    pushed_id, offset = read_u64(bob_push[5], offset)
    sender_id, offset = read_u64(bob_push[5], offset)
    nickname, offset = read_string(bob_push[5], offset)
    content, offset = read_string(bob_push[5], offset)
    created_at, offset = read_string(bob_push[5], offset)
    reply_to, reply_sender, reply_content, offset = parse_push_reply(bob_push[5], offset)
    if reply_to != first_id or reply_content != "original room text":
        raise AssertionError(f"room quote missing: {reply_to} {reply_content}")
    print("PASS room quote reply")

    alice.send(CMD_MESSAGE_RECALL, struct.pack("!BQQ", 1, 1, quoted_id))
    recall_response = alice.recv()
    recall_push = bob.recv()
    if recall_response[3] != CMD_MESSAGE_RECALL or recall_push[3] != CMD_MESSAGE_RECALL:
        raise AssertionError("room recall failed")
    print("PASS room recall")

    rows = search(alice, 1, 1, "original")
    if not any(row[0] == first_id for row in rows):
        raise AssertionError(f"room search failed: {rows}")
    print("PASS room search")

    private_first, _ = send_private(alice, b_id, "private original", str(uuid.uuid4()))
    private_push = bob.recv()
    if private_push[3] != CMD_PRIVATE_PUSH:
        raise AssertionError("private push missing")

    private_quote, _ = send_private(
        bob, a_id, "private quoted", str(uuid.uuid4()), private_first, "Alice",
        "private original")
    push = alice.recv()
    if push[3] != CMD_PRIVATE_PUSH:
        raise AssertionError("private quote push missing")
    from_id, offset = read_u64(push[5], 0)
    pushed_id, offset = read_u64(push[5], offset)
    nickname, offset = read_string(push[5], offset)
    content, offset = read_string(push[5], offset)
    created_at, offset = read_string(push[5], offset)
    reply_to, reply_sender, reply_content, offset = parse_push_reply(push[5], offset)
    if reply_to != private_first or reply_content != "private original":
        raise AssertionError("private quote metadata missing")
    print("PASS private quote reply")

    bob.send(CMD_MESSAGE_RECALL, struct.pack("!BQQ", 2, a_id, private_quote))
    recall_response = bob.recv()
    recall_push = alice.recv()
    if recall_response[3] != CMD_MESSAGE_RECALL or recall_push[3] != CMD_MESSAGE_RECALL:
        raise AssertionError("private recall failed")
    print("PASS private recall")

    rows = search(alice, 2, b_id, "private")
    if not any(row[0] == private_first for row in rows):
        raise AssertionError(f"private search failed: {rows}")
    print("PASS private search")

    exercise_long_nickname_replies(alice, bob, 1, a_id, b_id)

    alice.close()
    bob.close()
    print("ALL MESSAGE FEATURE CHECKS PASSED", a_id, b_id)
    return 0


if __name__ == "__main__":
    sys.exit(main())

