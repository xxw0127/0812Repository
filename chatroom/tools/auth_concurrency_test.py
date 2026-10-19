#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time

MAGIC = 0x4348
HEADER = struct.Struct("!HBBHII")
CMD_REGISTER = 0x0001
CMD_LOGIN = 0x0002
CMD_RESUME = 0x0004
CMD_ERROR = 0x00FF


def enc(value: str) -> bytes:
    data = value.encode()
    return struct.pack("!H", len(data)) + data


def frame(cmd: int, seq: int, body: bytes = b"") -> bytes:
    return HEADER.pack(MAGIC, 1, 0, cmd, seq, len(body)) + body


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("closed")
        data += chunk
    return data


def read_frame(sock):
    magic, version, flags, cmd, seq, length = HEADER.unpack(recv_exact(sock, HEADER.size))
    return magic, version, flags, cmd, seq, recv_exact(sock, length)


def connect(host, port):
    sock = socket.create_connection((host, port), timeout=5)
    sock.settimeout(5)
    return sock


def read_string(body, offset):
    length = struct.unpack_from("!H", body, offset)[0]
    offset += 2
    return body[offset : offset + length].decode(), offset + length


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--connections", type=int, default=8)
    args = parser.parse_args()

    username = f"c{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    password = "TestPass123!"
    nickname = "Concurrent"

    reg = connect(args.host, args.port)
    reg.sendall(frame(CMD_REGISTER, 1, enc(username) + enc(password) + enc(nickname)))
    response = read_frame(reg)
    if response[3] != CMD_REGISTER or response[2] & 0x04:
        print("FAIL register", response)
        return 1
    reg.close()
    print("registered", username)

    sockets = []
    for i in range(args.connections):
        sock = connect(args.host, args.port)
        sockets.append(sock)
        sock.sendall(frame(CMD_LOGIN, 100 + i, enc(username) + enc(password)))

    tokens = []
    kicked = 0
    for sock in sockets:
        try:
            response = read_frame(sock)
            if response[3] == CMD_LOGIN and not (response[2] & 0x04):
                offset = 8  # skip user_id
                _, offset = read_string(response[5], offset)
                token, _ = read_string(response[5], offset)
                tokens.append(token)
            elif response[3] == CMD_ERROR:
                kicked += 1
            else:
                print("FAIL unexpected response", response)
                return 1
        except (ConnectionError, OSError, socket.timeout):
            kicked += 1
        finally:
            sock.close()

    print(f"login responses: success={len(tokens)} kicked={kicked}")
    if not tokens:
        print("FAIL no successful login")
        return 1

    active = 0
    for token in tokens:
        sock = connect(args.host, args.port)
        sock.sendall(frame(CMD_RESUME, 900, enc(token)))
        try:
            response = read_frame(sock)
            if response[3] == CMD_RESUME and not (response[2] & 0x04):
                active += 1
        except (ConnectionError, OSError, socket.timeout):
            pass
        finally:
            sock.close()

    print(f"active sessions after concurrent logins: {active}")
    return 0 if active == 1 else 1


if __name__ == "__main__":
    sys.exit(main())
