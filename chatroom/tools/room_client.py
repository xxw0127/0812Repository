#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time
import uuid

MAGIC = 0x4348
HEADER = struct.Struct("!HBBHII")

CMD_REGISTER = 0x0001
CMD_LOGIN = 0x0002
CMD_ROOM_LIST = 0x0010
CMD_JOIN_ROOM = 0x0011
CMD_LEAVE_ROOM = 0x0012
CMD_ROOM_MEMBERS = 0x0013
CMD_CHAT_SEND = 0x0020
CMD_CHAT_SEND_RESP = 0x0021
CMD_CHAT_PUSH = 0x0022
CMD_ERROR = 0x00FF

FLAG_RESPONSE = 0x01
FLAG_ERROR = 0x04


def enc(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def u32(value: int) -> bytes:
    return struct.pack("!I", value)


def make_frame(cmd: int, seq: int, body: bytes = b"") -> bytes:
    return HEADER.pack(MAGIC, 1, 0, cmd, seq, len(body)) + body


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data += chunk
    return data


def read_frame(sock):
    magic, version, flags, cmd, seq, length = HEADER.unpack(recv_exact(sock, HEADER.size))
    return magic, version, flags, cmd, seq, recv_exact(sock, length)


def read_string(body, offset):
    length = struct.unpack_from("!H", body, offset)[0]
    offset += 2
    return body[offset : offset + length].decode("utf-8"), offset + length


def read_u32(body, offset):
    return struct.unpack_from("!I", body, offset)[0], offset + 4


def read_u64(body, offset):
    return struct.unpack_from("!Q", body, offset)[0], offset + 8


class Client:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(5)
        self.seq = 0

    def send(self, cmd, body=b""):
        self.seq += 1
        self.sock.sendall(make_frame(cmd, self.seq, body))
        return self.seq

    def recv(self):
        return read_frame(self.sock)

    def close(self):
        self.sock.close()

    def register(self, username, password, nickname):
        seq = self.send(CMD_REGISTER, enc(username) + enc(password) + enc(nickname))
        frame = self.recv()
        if frame[3] != CMD_REGISTER or frame[2] & FLAG_ERROR:
            raise AssertionError(f"register failed: {frame}")
        user_id, _ = read_u64(frame[5], 0)
        return user_id

    def login(self, username, password):
        seq = self.send(CMD_LOGIN, enc(username) + enc(password))
        frame = self.recv()
        if frame[3] != CMD_LOGIN or frame[2] & FLAG_ERROR:
            raise AssertionError(f"login failed: {frame}")
        user_id, offset = read_u64(frame[5], 0)
        nickname, offset = read_string(frame[5], offset)
        token, offset = read_string(frame[5], offset)
        return user_id, nickname, token


def read_error(sock):
    frame = read_frame(sock)
    if frame[3] != CMD_ERROR or not (frame[2] & FLAG_ERROR):
        raise AssertionError(f"expected error frame, got {frame}")
    return struct.unpack_from("!H", frame[5], 0)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    args = parser.parse_args()

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"a{suffix}"
    user_b = f"b{suffix}"
    password = "TestPass123!"

    reg = Client(args.host, args.port)
    reg.register(user_a, password, "Alice")
    reg.register(user_b, password, "Bob")
    reg.close()
    print("PASS register two users")

    alice = Client(args.host, args.port)
    bob = Client(args.host, args.port)
    a_id, a_nick, _ = alice.login(user_a, password)
    b_id, b_nick, _ = bob.login(user_b, password)
    print("PASS login two users", a_id, b_id)

    alice.send(CMD_ROOM_LIST)
    frame = alice.recv()
    if frame[3] != CMD_ROOM_LIST or frame[2] & FLAG_ERROR:
        raise AssertionError(f"room list failed: {frame}")
    count = struct.unpack_from("!H", frame[5], 0)[0]
    offset = 2
    rooms = []
    for _ in range(count):
        room_id, offset = read_u32(frame[5], offset)
        name, offset = read_string(frame[5], offset)
        max_members, offset = read_u32(frame[5], offset)
        online, offset = read_u32(frame[5], offset)
        rooms.append((room_id, name, max_members, online))
    if not rooms:
        raise AssertionError("empty room list")
    room_id = rooms[0][0]
    print("PASS room list", count, "rooms, using", room_id, rooms[0][1])

    for client in (alice, bob):
        client.send(CMD_JOIN_ROOM, u32(room_id))
        frame = client.recv()
        if frame[3] != CMD_JOIN_ROOM or frame[2] & FLAG_ERROR:
            raise AssertionError(f"join failed: {frame}")
    print("PASS join room")

    alice.send(CMD_ROOM_MEMBERS, u32(room_id))
    frame = alice.recv()
    if frame[3] != CMD_ROOM_MEMBERS or frame[2] & FLAG_ERROR:
        raise AssertionError(f"members failed: {frame}")
    _, offset = read_u32(frame[5], 0)
    member_count = struct.unpack_from("!H", frame[5], offset)[0]
    if member_count < 2:
        raise AssertionError(f"expected at least 2 members, got {member_count}")
    print("PASS room members", member_count)

    client_msg_id = str(uuid.uuid4())
    alice.send(CMD_CHAT_SEND, u32(room_id) + enc(client_msg_id) + enc("hello room"))
    alice_frames = [alice.recv(), alice.recv()]
    bob_frame = bob.recv()
    response = [f for f in alice_frames if f[3] == CMD_CHAT_SEND_RESP]
    push = [f for f in alice_frames if f[3] == CMD_CHAT_PUSH]
    if not response or not push or bob_frame[3] != CMD_CHAT_PUSH:
        raise AssertionError(f"chat frames wrong: {alice_frames} {bob_frame}")
    server_msg_id, offset = read_u64(response[0][5], 0)
    _, offset = read_string(response[0][5], offset)
    print("PASS group chat response", server_msg_id)

    # Parse Bob's push: room_id, message_id, sender_id, nickname, content, created_at.
    _, offset = read_u32(bob_frame[5], 0)
    pushed_id, offset = read_u64(bob_frame[5], offset)
    sender_id, offset = read_u64(bob_frame[5], offset)
    sender_nick, offset = read_string(bob_frame[5], offset)
    content, offset = read_string(bob_frame[5], offset)
    created_at, offset = read_string(bob_frame[5], offset)
    if pushed_id != server_msg_id or sender_id != a_id or sender_nick != a_nick:
        raise AssertionError("push metadata mismatch")
    if content != "hello room":
        raise AssertionError("push content mismatch")
    print("PASS group broadcast", pushed_id, created_at)

    # Duplicate client_msg_id must return the same id and must not broadcast again.
    alice.send(CMD_CHAT_SEND, u32(room_id) + enc(client_msg_id) + enc("hello room"))
    duplicate_response = alice.recv()
    if duplicate_response[3] != CMD_CHAT_SEND_RESP or duplicate_response[2] & FLAG_ERROR:
        raise AssertionError(f"duplicate message failed: {duplicate_response}")
    duplicate_id, _ = read_u64(duplicate_response[5], 0)
    if duplicate_id != server_msg_id:
        raise AssertionError(f"duplicate id mismatch: {duplicate_id} != {server_msg_id}")
    bob.sock.settimeout(0.8)
    try:
        extra = bob.recv()
        raise AssertionError(f"duplicate message was rebroadcast: {extra}")
    except socket.timeout:
        pass
    finally:
        bob.sock.settimeout(5)
    print("PASS duplicate client_msg_id")

    bob.send(CMD_CHAT_SEND, u32(room_id) + enc(str(uuid.uuid4())) + enc("reply"))
    bob_frames = [bob.recv(), bob.recv()]
    alice_reply = alice.recv()
    if not any(f[3] == CMD_CHAT_PUSH for f in bob_frames) or alice_reply[3] != CMD_CHAT_PUSH:
        raise AssertionError("reply broadcast failed")
    print("PASS second client broadcast")

    bob.send(CMD_LEAVE_ROOM, u32(room_id))
    frame = bob.recv()
    if frame[3] != CMD_LEAVE_ROOM or frame[2] & FLAG_ERROR:
        raise AssertionError(f"leave failed: {frame}")
    bob.send(CMD_CHAT_SEND, u32(room_id) + enc(str(uuid.uuid4())) + enc("should fail"))
    if read_error(bob.sock) != 1007:
        raise AssertionError("expected 1007 after leave")
    print("PASS leave and not-in-room rejection")

    alice.send(CMD_LEAVE_ROOM, u32(room_id))
    alice.recv()
    alice.close()
    bob.close()
    print("ALL ROOM CHECKS PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        sys.exit(1)
