#!/usr/bin/env bash
set -euo pipefail

sudo mkdir -p /etc/nginx/stream.d
sudo install -m 644 /tmp/nginx-chat-stream.conf /etc/nginx/stream.d/chat.conf

if ! grep -qE '^[[:space:]]*stream[[:space:]]*\{' /etc/nginx/nginx.conf; then
    sudo tee -a /etc/nginx/nginx.conf < /tmp/nginx-stream-include.conf >/dev/null
fi

sudo nginx -t
sudo systemctl reload nginx
