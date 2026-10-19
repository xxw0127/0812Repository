#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time
import uuid

from room_client import (
    CMD_CHAT_PUSH,
    CMD_CHAT_SEND,
    CMD_CHAT_SEND_RESP,
    CMD_JOIN_ROOM,
    CMD_ROOM_MEMBERS,
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
CMD_PRIVATE_CONVERSATIONS = 0x0033
CMD_PRIVATE_MARK_READ = 0x0034
CMD_HISTORY = 0x0050
CMD_HISTORY_RESP = 0x0051
CMD_ERROR = 0x00FF
FLAG_ERROR = 0x04


def read_u8(body, offset):
    return body[offset], offset + 1


def read_u16(body, offset):
    return struct.unpack_from("!H", body, offset)[0], offset + 2


def read_conversations(client):
    client.send(CMD_PRIVATE_CONVERSATIONS)
    frame = client.recv()
    if frame[3] != CMD_PRIVATE_CONVERSATIONS or frame[2] & FLAG_ERROR:
        raise AssertionError(f"conversations failed: {frame}")
    count = struct.unpack_from("!H", frame[5], 0)[0]
    offset = 2
    rows = []
    for _ in range(count):
        peer_id, offset = read_u64(frame[5], offset)
        peer_nick, offset = read_string(frame[5], offset)
        last_id, offset = read_u64(frame[5], offset)
        last_content, offset = read_string(frame[5], offset)
        last_time, offset = read_string(frame[5], offset)
        online, offset = read_u8(frame[5], offset)
        unread = struct.unpack_from("!I", frame[5], offset)[0]
        offset += 4
        rows.append((peer_id, peer_nick, last_id, last_content, last_time, online, unread))
    return rows


def read_error(sock):
    frame = read_frame(sock)
    if frame[3] != CMD_ERROR or not (frame[2] & FLAG_ERROR):
        raise AssertionError(f"expected error frame, got {frame}")
    return struct.unpack_from("!H", frame[5], 0)[0]


def read_frame(sock):
    from room_client import HEADER, recv_exact

    magic, version, flags, cmd, seq, length = HEADER.unpack(recv_exact(sock, HEADER.size))
    return magic, version, flags, cmd, seq, recv_exact(sock, length)


def private_history(client, peer_id, before_id=0, limit=2):
    body = struct.pack("!BQQH", 2, peer_id, before_id, limit)
    client.send(CMD_HISTORY, body)
    frame = client.recv()
    if frame[3] != CMD_HISTORY_RESP or frame[2] & FLAG_ERROR:
        raise AssertionError(f"private history failed: {frame}")
    kind, offset = read_u8(frame[5], 0)
    target, offset = read_u64(frame[5], offset)
    count, offset = read_u16(frame[5], offset)
    has_more, offset = read_u8(frame[5], offset)
    next_before, offset = read_u64(frame[5], offset)
    messages = []
    for _ in range(count):
        message_id, offset = read_u64(frame[5], offset)
        sender_id, offset = read_u64(frame[5], offset)
        nickname, offset = read_string(frame[5], offset)
        content, offset = read_string(frame[5], offset)
        created_at, offset = read_string(frame[5], offset)
        reply_to, offset = read_u64(frame[5], offset)
        reply_sender, offset = read_string(frame[5], offset)
        reply_content, offset = read_string(frame[5], offset)
        recalled, offset = read_u8(frame[5], offset)
        messages.append((message_id, sender_id, nickname, content, created_at,
                         reply_to, reply_sender, reply_content, recalled))
    return kind, target, messages, has_more, next_before


def room_history(client, room_id, before_id=0, limit=2):
    body = struct.pack("!BQQH", 1, room_id, before_id, limit)
    client.send(CMD_HISTORY, body)
    frame = client.recv()
    if frame[3] != CMD_HISTORY_RESP or frame[2] & FLAG_ERROR:
        raise AssertionError(f"room history failed: {frame}")
    kind, offset = read_u8(frame[5], 0)
    target, offset = read_u64(frame[5], offset)
    count, offset = read_u16(frame[5], offset)
    has_more, offset = read_u8(frame[5], offset)
    next_before, offset = read_u64(frame[5], offset)
    messages = []
    for _ in range(count):
        message_id, offset = read_u64(frame[5], offset)
        sender_id, offset = read_u64(frame[5], offset)
        nickname, offset = read_string(frame[5], offset)
        content, offset = read_string(frame[5], offset)
        created_at, offset = read_string(frame[5], offset)
        reply_to, offset = read_u64(frame[5], offset)
        reply_sender, offset = read_string(frame[5], offset)
        reply_content, offset = read_string(frame[5], offset)
        recalled, offset = read_u8(frame[5], offset)
        messages.append((message_id, sender_id, nickname, content, created_at,
                         reply_to, reply_sender, reply_content, recalled))
    return kind, target, messages, has_more, next_before


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"p{suffix}a"
    user_b = f"p{suffix}b"
    password = "TestPass123!"

    setup = Client(args.host, args.port)
    setup.register(user_a, password, "PrivateA")
    setup.register(user_b, password, "PrivateB")
    setup.close()

    alice = Client(args.host, args.port)
    bob = Client(args.host, args.port)
    a_id, a_nick, _ = alice.login(user_a, password)
    b_id, b_nick, _ = bob.login(user_b, password)
    print("PASS login", a_id, b_id)

    first_id = str(uuid.uuid4())
    alice.send(CMD_PRIVATE_SEND, struct.pack("!Q", b_id) + enc(first_id) + enc("hello bob"))
    response = alice.recv()
    push = bob.recv()
    if response[3] != CMD_PRIVATE_SEND_RESP or response[2] & FLAG_ERROR:
        raise AssertionError(f"private send failed: {response}")
    if push[3] != CMD_PRIVATE_PUSH:
        raise AssertionError(f"private push missing: {push}")
    message_id, _ = read_u64(response[5], 0)
    from_id, offset = read_u64(push[5], 0)
    pushed_id, offset = read_u64(push[5], offset)
    sender_nick, offset = read_string(push[5], offset)
    content, offset = read_string(push[5], offset)
    created_at, offset = read_string(push[5], offset)
    if from_id != a_id or pushed_id != message_id or sender_nick != a_nick:
        raise AssertionError("private push metadata mismatch")
    print("PASS private send and push")

    bob.send(CMD_PRIVATE_SEND, struct.pack("!Q", a_id) + enc(str(uuid.uuid4())) + enc("hi alice"))
    response = bob.recv()
    push = alice.recv()
    if response[3] != CMD_PRIVATE_SEND_RESP or push[3] != CMD_PRIVATE_PUSH:
        raise AssertionError("private reply failed")
    print("PASS private reply")

    peers = read_conversations(alice)
    if not any(peer[0] == b_id and peer[5] == 1 for peer in peers):
        raise AssertionError(f"conversation list missing peer: {peers}")
    print("PASS conversation list")

    # Send five more messages so pagination has multiple pages.
    for i in range(5):
        alice.send(
            CMD_PRIVATE_SEND,
            struct.pack("!Q", b_id) + enc(str(uuid.uuid4())) + enc(f"page-{i}"),
        )
        if alice.recv()[3] != CMD_PRIVATE_SEND_RESP:
            raise AssertionError("page message response failed")
        if bob.recv()[3] != CMD_PRIVATE_PUSH:
            raise AssertionError("page message push failed")

    bob_peers = read_conversations(bob)
    bob_row = next((row for row in bob_peers if row[0] == a_id), None)
    if bob_row is None or bob_row[6] == 0:
        raise AssertionError(f"unread count not increased: {bob_peers}")
    private_history(bob, a_id, 0, 10)
    bob_peers = read_conversations(bob)
    bob_row = next((row for row in bob_peers if row[0] == a_id), None)
    if bob_row is None or bob_row[6] != 0:
        raise AssertionError(f"unread count not cleared: {bob_peers}")
    print("PASS unread count and mark read")

    alice.send(
        CMD_PRIVATE_SEND,
        struct.pack("!Q", b_id) + enc(str(uuid.uuid4())) + enc("explicit mark read"),
    )
    if alice.recv()[3] != CMD_PRIVATE_SEND_RESP:
        raise AssertionError("explicit mark read setup failed")
    push = bob.recv()
    if push[3] != CMD_PRIVATE_PUSH:
        raise AssertionError("explicit mark read push missing")
    _, offset = read_u64(push[5], 0)
    mark_message_id, _ = read_u64(push[5], offset)
    bob.send(
        CMD_PRIVATE_MARK_READ,
        struct.pack("!Q", a_id) + struct.pack("!Q", mark_message_id),
    )
    response = bob.recv()
    if response[3] != CMD_PRIVATE_MARK_READ or response[2] & FLAG_ERROR:
        raise AssertionError(f"mark read command failed: {response}")
    bob_peers = read_conversations(bob)
    bob_row = next((row for row in bob_peers if row[0] == a_id), None)
    if bob_row is None or bob_row[6] != 0:
        raise AssertionError(f"explicit mark read did not clear: {bob_peers}")
    print("PASS explicit private mark read command")

    kind, target, page1, has_more, next_before = private_history(alice, b_id, 0, 2)
    if kind != 2 or target != b_id or len(page1) != 2 or not has_more:
        raise AssertionError(f"page1 wrong: {kind} {target} {len(page1)} {has_more}")
    kind, target, page2, has_more2, next2 = private_history(alice, b_id, next_before, 2)
    if len(page2) != 2 or page1[0][0] <= page2[-1][0]:
        raise AssertionError(f"page2 wrong: {page1} {page2}")
    print("PASS private history pagination", page1[0][0], page2[-1][0], has_more2, next2)

    # Duplicate private client_msg_id: same id, no second push.
    duplicate_id = str(uuid.uuid4())
    alice.send(CMD_PRIVATE_SEND, struct.pack("!Q", b_id) + enc(duplicate_id) + enc("dup"))
    first_response = alice.recv()
    first_push = bob.recv()
    if first_response[3] != CMD_PRIVATE_SEND_RESP or first_push[3] != CMD_PRIVATE_PUSH:
        raise AssertionError("duplicate setup failed")
    duplicate_message_id, _ = read_u64(first_response[5], 0)
    alice.send(CMD_PRIVATE_SEND, struct.pack("!Q", b_id) + enc(duplicate_id) + enc("dup"))
    second_response = alice.recv()
    second_id, _ = read_u64(second_response[5], 0)
    if second_id != duplicate_message_id:
        raise AssertionError("duplicate private id mismatch")
    bob.sock.settimeout(0.8)
    try:
        extra = bob.recv()
        raise AssertionError(f"duplicate private push: {extra}")
    except socket.timeout:
        pass
    finally:
        bob.sock.settimeout(5)
    print("PASS duplicate private client_msg_id")

    # Room history pagination and membership check.
    alice.send(CMD_JOIN_ROOM, u32(1))
    alice.recv()
    for i in range(5):
        alice.send(
            CMD_CHAT_SEND,
            u32(1) + enc(str(uuid.uuid4())) + enc(f"room-page-{i}"),
        )
        alice.recv()
        alice.recv()  # response + own push
    kind, target, room_page1, room_has_more, room_next = room_history(alice, 1, 0, 2)
    if kind != 1 or target != 1 or len(room_page1) != 2:
        raise AssertionError(f"room history page1 wrong: {room_page1}")
    kind, target, room_page2, room_has_more2, room_next2 = room_history(
        alice, 1, room_next, 2
    )
    if len(room_page2) != 2:
        raise AssertionError("room history page2 wrong")
    print("PASS room history pagination", room_has_more, room_has_more2)

    alice.send(CMD_HISTORY, struct.pack("!BQQH", 1, 2, 0, 2))
    if read_error(alice.sock) != 1007:
        raise AssertionError("expected 1007 for unjoined room history")
    print("PASS room history membership check")

    # Offline private message should persist and appear after re-login.
    bob.close()
    time.sleep(0.3)
    alice.send(
        CMD_PRIVATE_SEND,
        struct.pack("!Q", b_id) + enc(str(uuid.uuid4())) + enc("offline message"),
    )
    response = alice.recv()
    if response[3] != CMD_PRIVATE_SEND_RESP or response[2] & FLAG_ERROR:
        raise AssertionError(f"offline send failed: {response}")
    offline_id, _ = read_u64(response[5], 0)
    bob = Client(args.host, args.port)
    bob.login(user_b, password)
    offline_page = private_history(bob, a_id, 0, 10)[2]
    if not any(message[0] == offline_id and message[3] == "offline message"
               for message in offline_page):
        raise AssertionError("offline message not in history")
    print("PASS offline private message persistence")

    alice.close()
    bob.close()
    print("ALL PRIVATE/HISTORY CHECKS PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        sys.exit(1)
