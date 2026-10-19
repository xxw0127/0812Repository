#!/usr/bin/env python3
import argparse
import sys

from room_client import Client


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--connections", type=int, default=25)
    parser.add_argument("--expected", type=int, default=20)
    args = parser.parse_args()

    clients = []
    accepted = 0
    try:
        for _ in range(args.connections):
            try:
                client = Client(args.host, args.port)
                client.send(0x0040)
                frame = client.recv()
                if frame[3] == 0x0040:
                    accepted += 1
                    clients.append(client)
                    continue
                client.close()
            except Exception:  # noqa: BLE001
                pass
        print(f"connections={args.connections} accepted={accepted} expected={args.expected}")
        return 0 if accepted == args.expected else 1
    finally:
        for client in clients:
            client.close()


if __name__ == "__main__":
    sys.exit(main())
