#ifndef CHATROOM_CLIENT_CORE_NETWORK_THREAD_H
#define CHATROOM_CLIENT_CORE_NETWORK_THREAD_H

#include <atomic>
#include <deque>
#include <mutex>

#include <QByteArray>
#include <QString>
#include <QThread>

#include "core/socket_compat.h"

struct ssl_ctx_st;
struct ssl_st;

class NetworkThread : public QThread {
    Q_OBJECT

public:
    NetworkThread(QString host, quint16 port, bool use_tls, QString ca_path,
                  QObject* parent = nullptr);
    ~NetworkThread() override;

    bool enqueueFrame(const QByteArray& frame);
    void requestStop();

signals:
    void connected();
    void disconnected(const QString& reason);
    void frameReceived(const QByteArray& frame);

protected:
    void run() override;

private:
    bool initializeTls(SocketHandle socket, std::string& error);
    bool sendAll(SocketHandle socket, const QByteArray& data, std::string& error);
    int receive(SocketHandle socket, char* buffer, int size, std::string& error);
    void shutdownTls();
    bool hasPendingTlsData() const;
    bool parseIncoming(std::string& error);

    QString host_;
    quint16 port_;
    bool use_tls_;
    QString ca_path_;
    ssl_ctx_st* ssl_ctx_ = nullptr;
    ssl_st* ssl_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> intentional_stop_{false};
    std::atomic<SocketHandle> active_socket_{kInvalidSocket};
    std::mutex queue_mutex_;
    std::deque<QByteArray> send_queue_;
    QByteArray receive_buffer_;
};

#endif  // CHATROOM_CLIENT_CORE_NETWORK_THREAD_H
