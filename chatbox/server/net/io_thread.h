#ifndef CHATBOX_SERVER_NET_IO_THREAD_H
#define CHATBOX_SERVER_NET_IO_THREAD_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "server/net/connection.h"

namespace chatbox {

class IoThread {
public:
    // Return false to close the connection after handling the frame.
    using FrameHandler = std::function<bool(const Connection::Ptr&, const Frame&, IoThread&)>;
    using CloseHandler = std::function<void(uint64_t)>;
    using ProxyRekeyHandler =
        std::function<bool(uint64_t, const std::string&, std::string&)>;

    IoThread(int id, FrameHandler handler, int idle_timeout_sec,
             CloseHandler close_handler = nullptr, bool proxy_protocol = false,
             ProxyRekeyHandler proxy_rekey_handler = nullptr);
    ~IoThread();

    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;

    bool start(std::string& error);
    void stop();
    bool addConnection(int fd, uint64_t connection_id, const std::string& peer_ip,
                       std::string& error);
    void post(std::function<void()> task);
    void postClose(uint64_t connection_id, const std::string& reason);
    void postErrorAndClose(uint64_t connection_id, uint16_t code, const std::string& message,
                           const std::string& reason);
    void postMulticast(const std::vector<uint64_t>& connection_ids, std::string frame);
    void sendFrame(const Connection::Ptr& connection, std::string frame);
    int id() const;

private:
    struct PendingConnection {
        int fd;
        uint64_t id;
        std::string peer_ip;
    };

    void run();
    void wakeup();
    void handlePending();
    void handleTasks();
    void handleTimer();
    void handleRead(const Connection::Ptr& connection);
    void handleWrite(const Connection::Ptr& connection);
    void updateEvents(const Connection::Ptr& connection);
    void closeConnection(const Connection::Ptr& connection, const std::string& reason);
    void closeAll();

    int id_;
    FrameHandler handler_;
    CloseHandler close_handler_;
    ProxyRekeyHandler proxy_rekey_handler_;
    int idle_timeout_sec_;
    bool proxy_protocol_ = false;
    int epoll_fd_ = -1;
    int event_fd_ = -1;
    int timer_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex pending_mutex_;
    std::vector<PendingConnection> pending_;
    std::mutex task_mutex_;
    std::vector<std::function<void()>> pending_tasks_;
    std::unordered_map<int, Connection::Ptr> connections_;
    std::unordered_map<uint64_t, Connection::Ptr> connections_by_id_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_NET_IO_THREAD_H
