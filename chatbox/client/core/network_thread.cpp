#include "core/network_thread.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <utility>

#include <QDateTime>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "common/protocol.h"
#include "core/protocol_qt.h"

namespace {

std::string tlsError(const char* operation, int ssl_error = 0) {
    const unsigned long code = ERR_get_error();
    char buffer[256] = {0};
    if (code != 0) {
        ERR_error_string_n(code, buffer, sizeof(buffer));
    } else {
        std::snprintf(buffer, sizeof(buffer), "ssl error %d", ssl_error);
    }
    return std::string(operation) + ": " + buffer;
}

bool isIpAddress(const QString& host) {
    addrinfo hints{};
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* result = nullptr;
    const QByteArray text = host.toUtf8();
    const bool numeric = getaddrinfo(text.constData(), nullptr, &hints, &result) == 0;
    if (result != nullptr) {
        freeaddrinfo(result);
    }
    return numeric;
}

bool waitForSocket(SocketHandle socket, bool write, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    const int ready = write
                          ? ::select(0, nullptr, &set, nullptr, &timeout)
                          : ::select(0, &set, nullptr, nullptr, &timeout);
#else
    const int ready = write
                          ? ::select(socket + 1, nullptr, &set, nullptr, &timeout)
                          : ::select(socket + 1, &set, nullptr, nullptr, &timeout);
#endif
    return ready > 0;
}

}  // namespace

NetworkThread::NetworkThread(QString host, quint16 port, bool use_tls, QString ca_path,
                             QObject* parent)
    : QThread(parent),
      host_(std::move(host)),
      port_(port),
      use_tls_(use_tls),
      ca_path_(std::move(ca_path)) {}

NetworkThread::~NetworkThread() {
    requestStop();
    wait();
}

bool NetworkThread::enqueueFrame(const QByteArray& frame) {
    if (!running_ || frame.isEmpty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    send_queue_.push_back(frame);
    return true;
}

void NetworkThread::requestStop() {
    intentional_stop_ = true;
    running_ = false;
    const SocketHandle socket = active_socket_.load();
    if (socket != kInvalidSocket) {
#ifdef _WIN32
        ::shutdown(socket, SD_BOTH);
#else
        ::shutdown(socket, SHUT_RDWR);
#endif
    }
}

bool NetworkThread::initializeTls(SocketHandle socket, std::string& error) {
    OPENSSL_init_ssl(0, nullptr);
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx_ == nullptr) {
        error = tlsError("SSL_CTX_new");
        return false;
    }
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_2_VERSION);
    const QByteArray ca = ca_path_.toUtf8();
    if (ca.isEmpty() ||
        SSL_CTX_load_verify_locations(ssl_ctx_, ca.constData(), nullptr) != 1) {
        error = "TLS CA load failed: " + ca_path_.toStdString();
        return false;
    }
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);

    ssl_ = SSL_new(ssl_ctx_);
    if (ssl_ == nullptr) {
        error = tlsError("SSL_new");
        return false;
    }
    SSL_set_fd(ssl_, static_cast<int>(socket));

    const QByteArray host = host_.toUtf8();
    SSL_set_tlsext_host_name(ssl_, host.constData());
    X509_VERIFY_PARAM* verify_param = SSL_get0_param(ssl_);
    if (isIpAddress(host_)) {
        X509_VERIFY_PARAM_set1_ip_asc(verify_param, host.constData());
    } else {
        X509_VERIFY_PARAM_set1_host(verify_param, host.constData(), 0);
    }

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 5000;
    while (true) {
        const int result = SSL_connect(ssl_);
        if (result == 1) {
            return true;
        }
        const int ssl_error = SSL_get_error(ssl_, result);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            const qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
            if (remaining <= 0) {
                error = "TLS handshake timeout";
                return false;
            }
            waitForSocket(socket, ssl_error == SSL_ERROR_WANT_WRITE,
                          static_cast<int>(std::min<qint64>(remaining, 1000)));
            continue;
        }
        const long verify_result = SSL_get_verify_result(ssl_);
        if (verify_result != X509_V_OK) {
            error = std::string("TLS certificate verify failed: ") +
                    X509_verify_cert_error_string(verify_result);
        } else {
            error = tlsError("SSL_connect", ssl_error);
        }
        return false;
    }
}

bool NetworkThread::sendAll(SocketHandle socket, const QByteArray& data,
                            std::string& error) {
    int sent = 0;
    while (sent < data.size()) {
        int written = 0;
        if (ssl_ != nullptr) {
            written = SSL_write(ssl_, data.constData() + sent, data.size() - sent);
            if (written <= 0) {
                const int ssl_error = SSL_get_error(ssl_, written);
                if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                    if (!waitForSocket(socket, ssl_error == SSL_ERROR_WANT_WRITE, 1000)) {
                        error = "TLS send timeout";
                        return false;
                    }
                    continue;
                }
                error = tlsError("SSL_write", ssl_error);
                return false;
            }
        } else {
            written = static_cast<int>(
                ::send(socket, data.constData() + sent, data.size() - sent, 0));
            if (written <= 0) {
                if (chatroom::socketWouldBlock()) {
                    if (!waitForSocket(socket, true, 1000)) {
                        error = "send timeout";
                        return false;
                    }
                    continue;
                }
                error = chatroom::lastSocketError();
                return false;
            }
        }
        sent += written;
    }
    return true;
}

int NetworkThread::receive(SocketHandle socket, char* buffer, int size,
                           std::string& error) {
    if (ssl_ != nullptr) {
        const int received = SSL_read(ssl_, buffer, size);
        if (received > 0) {
            return received;
        }
        const int ssl_error = SSL_get_error(ssl_, received);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            return -2;
        }
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        error = tlsError("SSL_read", ssl_error);
        return -1;
    }

    const int received = static_cast<int>(::recv(socket, buffer, size, 0));
    if (received > 0) {
        return received;
    }
    if (received == 0) {
        return 0;
    }
    if (chatroom::socketWouldBlock()) {
        return -2;
    }
    error = chatroom::lastSocketError();
    return -1;
}

void NetworkThread::shutdownTls() {
    if (ssl_ != nullptr) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_ != nullptr) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

bool NetworkThread::hasPendingTlsData() const {
    return ssl_ != nullptr && SSL_pending(ssl_) > 0;
}

void NetworkThread::run() {
    std::string error;
    SocketHandle socket = kInvalidSocket;
    if (!chatroom::connectWithTimeout(chatroom::toUtf8(host_), port_, 5000, socket, error)) {
        running_ = false;
        emit disconnected(QString::fromStdString(error));
        return;
    }

    active_socket_.store(socket);
    if (!chatroom::setSocketNonBlocking(socket, true)) {
        error = "failed to set socket non-blocking";
        running_ = false;
        active_socket_.store(kInvalidSocket);
        chatroom::closeSocket(socket);
        emit disconnected(QString::fromStdString(error));
        return;
    }
    if (use_tls_ && !initializeTls(socket, error)) {
        running_ = false;
        active_socket_.store(kInvalidSocket);
        shutdownTls();
        chatroom::closeSocket(socket);
        emit disconnected(QString::fromStdString(error));
        return;
    }

    emit connected();
    QString reason;
    qint64 last_heartbeat = QDateTime::currentMSecsSinceEpoch();

    while (running_) {
        std::deque<QByteArray> pending;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending.swap(send_queue_);
        }
        for (const QByteArray& frame : pending) {
            if (!sendAll(socket, frame, error)) {
                reason = QString::fromStdString(error);
                running_ = false;
                break;
            }
        }
        if (!running_) {
            break;
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - last_heartbeat >= 30000) {
            chatroom::Frame ping;
            ping.header.cmd = chatroom::kCmdPing;
            ping.header.seq = 0;
            const QByteArray frame = chatroom::encodeToByteArray(ping);
            if (!sendAll(socket, frame, error)) {
                reason = QString::fromStdString(error);
                break;
            }
            last_heartbeat = now;
        }

        if (!hasPendingTlsData()) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(socket, &read_set);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;
#ifdef _WIN32
            const int ready = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
            const int ready = ::select(socket + 1, &read_set, nullptr, nullptr, &timeout);
#endif
            if (ready < 0) {
                if (chatroom::socketWouldBlock()) {
                    continue;
                }
                reason = QString::fromStdString(chatroom::lastSocketError());
                break;
            }
            if (ready == 0) {
                continue;
            }
        }

        char buffer[8192];
        const int received = receive(socket, buffer, sizeof(buffer), error);
        if (received > 0) {
            receive_buffer_.append(buffer, received);
            if (!parseIncoming(error)) {
                reason = QString::fromStdString(error);
                break;
            }
        } else if (received == 0) {
            reason = "服务器关闭连接";
            break;
        } else if (received == -1) {
            reason = QString::fromStdString(error);
            break;
        }
    }

    running_ = false;
    active_socket_.store(kInvalidSocket);
    shutdownTls();
    chatroom::closeSocket(socket);
    if (intentional_stop_) {
        reason = "已断开";
    } else if (reason.isEmpty()) {
        reason = "连接已关闭";
    }
    emit disconnected(reason);
}

bool NetworkThread::parseIncoming(std::string& error) {
    while (receive_buffer_.size() >= static_cast<int>(chatroom::kFrameHeaderSize)) {
        chatroom::FrameHeader header;
        if (!chatroom::decodeHeader(receive_buffer_.constData(),
                                    chatroom::kFrameHeaderSize, header)) {
            error = "invalid frame header";
            receive_buffer_.clear();
            return false;
        }
        if (!chatroom::validateHeader(header, chatroom::kMaxInFrameBody, error)) {
            receive_buffer_.clear();
            return false;
        }
        const int total = static_cast<int>(chatroom::kFrameHeaderSize + header.body_len);
        if (receive_buffer_.size() < total) {
            break;
        }
        const QByteArray frame = receive_buffer_.left(total);
        receive_buffer_.remove(0, total);
        emit frameReceived(frame);
    }
    return true;
}
