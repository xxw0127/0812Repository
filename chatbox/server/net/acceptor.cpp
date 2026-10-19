#include "server/net/acceptor.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server/util/logging.h"

namespace chatbox {

namespace {
constexpr int kListenBacklog = 1024;
constexpr int kMaxEvents = 16;
}  // namespace

Acceptor::Acceptor(int port, std::vector<IoThread*> io_threads, ConnectionLimiter& limiter,
                   std::string listen_address)
    : port_(port),
      io_threads_(std::move(io_threads)),
      limiter_(limiter),
      listen_address_(std::move(listen_address)) {}

Acceptor::~Acceptor() {
    stop();
}

bool Acceptor::start(std::string& error) {
    if (io_threads_.empty()) {
        error = "no IO threads configured";
        return false;
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        error = std::string("socket failed: ") + std::strerror(errno);
        return false;
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    const std::string bind_address =
        listen_address_.empty() ? std::string("0.0.0.0") : listen_address_;
    if (::inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1) {
        error = "invalid listen address: " + bind_address;
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        error = std::string("bind failed: ") + std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    socklen_t address_length = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_length) == 0) {
        port_ = ntohs(address.sin_port);
    }

    if (::listen(listen_fd_, kListenBacklog) < 0) {
        error = std::string("listen failed: ") + std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (epoll_fd_ < 0 || event_fd_ < 0) {
        error = std::string("epoll/eventfd failed: ") + std::strerror(errno);
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
        if (event_fd_ >= 0) {
            ::close(event_fd_);
            event_fd_ = -1;
        }
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listen_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) < 0) {
        error = std::string("epoll add listen fd failed: ") + std::strerror(errno);
        ::close(epoll_fd_);
        ::close(event_fd_);
        ::close(listen_fd_);
        epoll_fd_ = event_fd_ = listen_fd_ = -1;
        return false;
    }

    event.events = EPOLLIN;
    event.data.fd = event_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event) < 0) {
        error = std::string("epoll add eventfd failed: ") + std::strerror(errno);
        ::close(epoll_fd_);
        ::close(event_fd_);
        ::close(listen_fd_);
        epoll_fd_ = event_fd_ = listen_fd_ = -1;
        return false;
    }

    running_ = true;
    thread_ = std::thread(&Acceptor::run, this);
    logMessage(LogLevel::kInfo, "acceptor",
               "listening on " + bind_address + ":" + std::to_string(port_) + " with " +
                   std::to_string(io_threads_.size()) + " IO threads");
    return true;
}

void Acceptor::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    wakeup();
    if (thread_.joinable()) {
        thread_.join();
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (event_fd_ >= 0) {
        ::close(event_fd_);
        event_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    logMessage(LogLevel::kInfo, "acceptor", "stopped");
}

int Acceptor::port() const {
    return port_;
}

void Acceptor::wakeup() {
    if (event_fd_ < 0) {
        return;
    }
    const uint64_t value = 1;
    const ssize_t written = ::write(event_fd_, &value, sizeof(value));
    (void)written;
}

void Acceptor::run() {
    std::vector<epoll_event> events(kMaxEvents);
    while (running_) {
        const int ready = ::epoll_wait(epoll_fd_, events.data(), kMaxEvents, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            logMessage(LogLevel::kError, "acceptor",
                       std::string("epoll_wait failed: ") + std::strerror(errno));
            break;
        }

        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd;
            if (fd == event_fd_) {
                uint64_t value = 0;
                while (::read(event_fd_, &value, sizeof(value)) > 0) {
                }
                continue;
            }
            if (fd != listen_fd_) {
                continue;
            }

            while (running_) {
                sockaddr_in client_address{};
                socklen_t client_length = sizeof(client_address);
                const int client_fd = ::accept4(
                    listen_fd_, reinterpret_cast<sockaddr*>(&client_address), &client_length,
                    SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client_fd < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    logMessage(LogLevel::kWarn, "acceptor",
                               std::string("accept failed: ") + std::strerror(errno));
                    break;
                }

                char client_ip[INET_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));
                const uint64_t connection_id = next_connection_id_++;
                IoThread* target = io_threads_[next_io_ % io_threads_.size()];
                ++next_io_;

                std::string limit_error;
                if (!limiter_.acquire(connection_id, client_ip, limit_error)) {
                    logMessage(LogLevel::kWarn, "acceptor",
                               "reject conn=" + std::to_string(connection_id) + " from " +
                                   client_ip + ": " + limit_error);
                    ::close(client_fd);
                    continue;
                }

                std::string error;
                if (!target->addConnection(client_fd, connection_id, client_ip, error)) {
                    limiter_.release(connection_id);
                    logMessage(LogLevel::kWarn, "acceptor",
                               "dispatch fd " + std::to_string(client_fd) + " failed: " + error);
                    ::close(client_fd);
                    continue;
                }
                logMessage(LogLevel::kDebug, "acceptor",
                           "accepted conn=" + std::to_string(connection_id) + " from " +
                               client_ip + ":" + std::to_string(ntohs(client_address.sin_port)) +
                               " -> io " + std::to_string(target->id()));
            }
        }
    }
}

}  // namespace chatbox
