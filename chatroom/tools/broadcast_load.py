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
    u32,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--messages", type=int, default=20)
    parser.add_argument("--room-id", type=int, default=11)
    args = parser.parse_args()

    if args.clients < 2:
        print("clients must be >= 2")
        return 2

    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    password = "TestPass123!"
    clients = []
    started = time.monotonic()
    try:
        for i in range(args.clients):
            client = Client(args.host, args.port)
            username = f"l{suffix}{i}"
            client.register(username, password, f"Load{i}")
            client.login(username, password)
            client.send(CMD_JOIN_ROOM, u32(args.room_id))
            response = client.recv()
            if response[3] != CMD_JOIN_ROOM or response[2] & 0x04:
                raise AssertionError(f"join failed for {username}: {response}")
            clients.append(client)
        print(f"joined {len(clients)} clients in {time.monotonic() - started:.2f}s")

        sender = clients[0]
        for i in range(args.messages):
            client_msg_id = str(uuid.uuid4())
            sender.send(
                CMD_CHAT_SEND,
                u32(args.room_id) + enc(client_msg_id) + enc(f"load-{i}"),
            )

        failures = 0
        for index, client in enumerate(clients):
            received = 0
            client.sock.settimeout(10)
            while received < args.messages:
                frame = client.recv()
                if frame[3] == CMD_CHAT_PUSH:
                    received += 1
                elif frame[3] == CMD_CHAT_SEND_RESP:
                    continue
                else:
                    raise AssertionError(f"unexpected frame for client {index}: {frame}")
            if received != args.messages:
                failures += 1

        elapsed = time.monotonic() - started
        print(
            f"clients={len(clients)} messages={args.messages} "
            f"deliveries={len(clients) * args.messages} failures={failures} "
            f"elapsed={elapsed:.2f}s"
        )
        return 0 if failures == 0 else 1
    finally:
        for client in clients:
            client.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        sys.exit(1)
