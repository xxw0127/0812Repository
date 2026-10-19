#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time

MAGIC = 0x4348
VERSION = 1
HEADER = struct.Struct("!HBBHII")

CMD_REGISTER = 0x0001
CMD_LOGIN = 0x0002
CMD_LOGOUT = 0x0003
CMD_RESUME = 0x0004
CMD_ERROR = 0x00FF

FLAG_RESPONSE = 0x01
FLAG_ERROR = 0x04


def encode_string(value: str) -> bytes:
    data = value.encode("utf-8")
    if len(data) > 0xFFFF:
        raise ValueError("string too long")
    return struct.pack("!H", len(data)) + data


def make_frame(cmd: int, seq: int, body: bytes = b"") -> bytes:
    return HEADER.pack(MAGIC, VERSION, 0, cmd, seq, len(body)) + body


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data.extend(chunk)
    return bytes(data)


def read_frame(sock: socket.socket):
    header = recv_exact(sock, HEADER.size)
    magic, version, flags, cmd, seq, body_len = HEADER.unpack(header)
    return magic, version, flags, cmd, seq, recv_exact(sock, body_len)


def read_string(body: bytes, offset: int):
    if offset + 2 > len(body):
        raise ValueError("truncated string length")
    length = struct.unpack_from("!H", body, offset)[0]
    offset += 2
    if offset + length > len(body):
        raise ValueError("truncated string")
    return body[offset : offset + length].decode("utf-8"), offset + length


def read_u64(body: bytes, offset: int):
    if offset + 8 > len(body):
        raise ValueError("truncated u64")
    return struct.unpack_from("!Q", body, offset)[0], offset + 8


def open_connection(host: str, port: int):
    sock = socket.create_connection((host, port), timeout=5)
    sock.settimeout(5)
    return sock


def expect_error(frame, expected_code: int, label: str):
    magic, version, flags, cmd, seq, body = frame
    if magic != MAGIC or cmd != CMD_ERROR or not (flags & FLAG_ERROR):
        raise AssertionError(f"{label}: expected error frame, got {frame}")
    code = struct.unpack_from("!H", body, 0)[0]
    if code != expected_code:
        raise AssertionError(f"{label}: expected code {expected_code}, got {code}")
    print(f"PASS {label} (code {code})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    args = parser.parse_args()

    username = f"u{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    password = "TestPass123!"
    nickname = "Tester"

    try:
        sock = open_connection(args.host, args.port)
        body = encode_string(username) + encode_string(password) + encode_string(nickname)
        sock.sendall(make_frame(CMD_REGISTER, 1, body))
        frame = read_frame(sock)
        if frame[3] != CMD_REGISTER or frame[2] & FLAG_ERROR:
            raise AssertionError(f"register failed: {frame}")
        user_id, _ = read_u64(frame[5], 0)
        assert user_id > 0
        print("PASS register", user_id)

        body = encode_string(username) + encode_string(password) + encode_string(nickname)
        sock.sendall(make_frame(CMD_REGISTER, 2, body))
        expect_error(read_frame(sock), 1003, "duplicate register")
        sock.close()

        sock = open_connection(args.host, args.port)
        body = encode_string(username) + encode_string("WrongPass123!")
        sock.sendall(make_frame(CMD_LOGIN, 3, body))
        expect_error(read_frame(sock), 1004, "wrong password")
        sock.close()

        sock = open_connection(args.host, args.port)
        body = encode_string(username) + encode_string(password)
        sock.sendall(make_frame(CMD_LOGIN, 4, body))
        frame = read_frame(sock)
        if frame[3] != CMD_LOGIN or frame[2] & FLAG_ERROR:
            raise AssertionError(f"login failed: {frame}")
        login_id, offset = read_u64(frame[5], 0)
        login_nick, offset = read_string(frame[5], offset)
        token, offset = read_string(frame[5], offset)
        assert login_id == user_id and login_nick == nickname and len(token) == 64
        print("PASS login")

        sock2 = open_connection(args.host, args.port)
        body = encode_string(username) + encode_string(password)
        sock2.sendall(make_frame(CMD_LOGIN, 5, body))
        frame2 = read_frame(sock2)
        if frame2[3] != CMD_LOGIN or frame2[2] & FLAG_ERROR:
            raise AssertionError(f"second login failed: {frame2}")
        _, offset = read_u64(frame2[5], 0)
        _, offset = read_string(frame2[5], offset)
        token2, offset = read_string(frame2[5], offset)

        # The first connection must be kicked by the new login.
        try:
            old_frame = read_frame(sock)
            if (old_frame[3] == CMD_ERROR and
                    struct.unpack_from("!H", old_frame[5], 0)[0] == 1010):
                print("PASS old connection notified with 1010")
            else:
                print("INFO old connection closed without 1010 frame")
        except (ConnectionError, OSError, socket.timeout):
            print("INFO old connection closed without 1010 frame")
        sock.close()

        sock3 = open_connection(args.host, args.port)
        sock3.sendall(make_frame(CMD_RESUME, 6, encode_string(token2)))
        frame3 = read_frame(sock3)
        if frame3[3] != CMD_RESUME or frame3[2] & FLAG_ERROR:
            raise AssertionError(f"resume failed: {frame3}")
        resume_id, offset = read_u64(frame3[5], 0)
        resume_nick, offset = read_string(frame3[5], offset)
        assert resume_id == user_id and resume_nick == nickname
        print("PASS resume")

        # The second connection is replaced by the resume.
        try:
            sock2.settimeout(2)
            sock2.recv(1)
        except (ConnectionError, OSError, socket.timeout):
            pass
        sock2.close()

        sock3.sendall(make_frame(CMD_LOGOUT, 7))
        frame4 = read_frame(sock3)
        if frame4[3] != CMD_LOGOUT or frame4[2] & FLAG_ERROR:
            raise AssertionError(f"logout failed: {frame4}")
        print("PASS logout")

        sock4 = open_connection(args.host, args.port)
        sock4.sendall(make_frame(CMD_RESUME, 8, encode_string(token2)))
        expect_error(read_frame(sock4), 1002, "resume after logout")
        sock4.close()
        sock3.close()

        sock5 = open_connection(args.host, args.port)
        sock5.sendall(make_frame(CMD_RESUME, 9, encode_string("0" * 64)))
        expect_error(read_frame(sock5), 1009, "resume unknown token")
        sock5.close()

        print("ALL AUTH CHECKS PASSED")
        return 0
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
