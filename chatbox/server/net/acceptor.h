#ifndef CHATBOX_SERVER_NET_ACCEPTOR_H
#define CHATBOX_SERVER_NET_ACCEPTOR_H

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "server/net/connection_limiter.h"
#include "server/net/io_thread.h"

namespace chatbox {

class Acceptor {
public:
    Acceptor(int port, std::vector<IoThread*> io_threads, ConnectionLimiter& limiter,
             std::string listen_address, bool proxy_protocol = false);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    bool start(std::string& error);
    void stop();
    int port() const;

private:
    void run();
    void wakeup();

    int port_;
    std::vector<IoThread*> io_threads_;
    ConnectionLimiter& limiter_;
    std::string listen_address_;
    bool proxy_protocol_ = false;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    int event_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
    uint64_t next_connection_id_ = 1;
    std::size_t next_io_ = 0;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_NET_ACCEPTOR_H
