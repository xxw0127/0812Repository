#!/usr/bin/env bash
set -euo pipefail

CERT_DIR=/etc/chatroom/certs
sudo mkdir -p "${CERT_DIR}"

if [ ! -f "${CERT_DIR}/ca.crt" ]; then
    sudo openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
        -keyout "${CERT_DIR}/ca.key" -out "${CERT_DIR}/ca.crt" \
        -subj "/CN=Chatroom Dev CA"
fi

sudo openssl req -newkey rsa:2048 -sha256 -nodes \
    -keyout "${CERT_DIR}/chat.key" -out "${CERT_DIR}/chat.csr" \
    -subj "/CN=chatroom.local"

sudo tee "${CERT_DIR}/ext.cnf" >/dev/null <<'EOF'
subjectAltName=IP:192.168.2.113,DNS:chatroom.local
extendedKeyUsage=serverAuth
EOF

sudo openssl x509 -req -in "${CERT_DIR}/chat.csr" \
    -CA "${CERT_DIR}/ca.crt" -CAkey "${CERT_DIR}/ca.key" -CAcreateserial \
    -out "${CERT_DIR}/chat.crt" -days 825 -sha256 \
    -extfile "${CERT_DIR}/ext.cnf"

sudo chmod 600 "${CERT_DIR}/ca.key" "${CERT_DIR}/chat.key"
sudo chmod 644 "${CERT_DIR}/ca.crt" "${CERT_DIR}/chat.crt"
sudo rm -f "${CERT_DIR}/chat.csr" "${CERT_DIR}/ext.cnf" "${CERT_DIR}/ca.srl"

echo "certificates ready in ${CERT_DIR}"
