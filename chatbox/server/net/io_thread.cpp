#include "server/net/io_thread.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "server/util/logging.h"
#include "server/util/metrics.h"

namespace chatbox {

namespace {
constexpr int kMaxEvents = 128;
constexpr std::size_t kReadChunk = 8192;
}  // namespace

IoThread::IoThread(int id, FrameHandler handler, int idle_timeout_sec,
                   CloseHandler close_handler, bool proxy_protocol)
    : id_(id),
      handler_(std::move(handler)),
      close_handler_(std::move(close_handler)),
      idle_timeout_sec_(idle_timeout_sec),
      proxy_protocol_(proxy_protocol) {}

IoThread::~IoThread() {
    stop();
}

bool IoThread::start(std::string& error) {
    if (running_) {
        return true;
    }

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (epoll_fd_ < 0 || event_fd_ < 0 || timer_fd_ < 0) {
        error = std::string("failed to create epoll/eventfd/timerfd: ") + std::strerror(errno);
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
        if (event_fd_ >= 0) {
            ::close(event_fd_);
            event_fd_ = -1;
        }
        if (timer_fd_ >= 0) {
            ::close(timer_fd_);
            timer_fd_ = -1;
        }
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = event_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event) < 0) {
        error = std::string("epoll add eventfd failed: ") + std::strerror(errno);
        ::close(epoll_fd_);
        ::close(event_fd_);
        ::close(timer_fd_);
        epoll_fd_ = event_fd_ = timer_fd_ = -1;
        return false;
    }

    event.events = EPOLLIN;
    event.data.fd = timer_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &event) < 0) {
        error = std::string("epoll add timerfd failed: ") + std::strerror(errno);
        ::close(epoll_fd_);
        ::close(event_fd_);
        ::close(timer_fd_);
        epoll_fd_ = event_fd_ = timer_fd_ = -1;
        return false;
    }

    itimerspec timer{};
    timer.it_interval.tv_sec = 1;
    timer.it_value.tv_sec = 1;
    if (::timerfd_settime(timer_fd_, 0, &timer, nullptr) < 0) {
        error = std::string("timerfd_settime failed: ") + std::strerror(errno);
        ::close(epoll_fd_);
        ::close(event_fd_);
        ::close(timer_fd_);
        epoll_fd_ = event_fd_ = timer_fd_ = -1;
        return false;
    }

    running_ = true;
    thread_ = std::thread(&IoThread::run, this);
    logMessage(LogLevel::kInfo, "io", "thread " + std::to_string(id_) + " started");
    return true;
}

void IoThread::stop() {
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
    if (timer_fd_ >= 0) {
        ::close(timer_fd_);
        timer_fd_ = -1;
    }
}

bool IoThread::addConnection(int fd, uint64_t connection_id, const std::string& peer_ip,
                             std::string& error) {
    if (!running_) {
        error = "io thread is not running";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.push_back(PendingConnection{fd, connection_id, peer_ip});
    }
    wakeup();
    return true;
}

void IoThread::post(std::function<void()> task) {
    if (!running_ || !task) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        pending_tasks_.push_back(std::move(task));
    }
    wakeup();
}

void IoThread::postClose(uint64_t connection_id, const std::string& reason) {
    post([this, connection_id, reason] {
        auto it = connections_by_id_.find(connection_id);
        if (it != connections_by_id_.end()) {
            closeConnection(it->second, reason);
        }
    });
}

void IoThread::postErrorAndClose(uint64_t connection_id, uint16_t code,
                                 const std::string& message, const std::string& reason) {
    post([this, connection_id, code, message, reason] {
        auto it = connections_by_id_.find(connection_id);
        if (it == connections_by_id_.end()) {
            return;
        }
        Frame response;
        response.header.flags = kFlagResponse | kFlagError;
        response.header.cmd = kCmdError;
        response.header.seq = 0;
        ByteWriter writer;
        writer.putU16(code);
        writer.putString(message);
        response.body = writer.data();
        if (!it->second->enqueue(encodeFrame(response))) {
            closeConnection(it->second, reason);
            return;
        }
        it->second->setCloseAfterFlush(true);
        updateEvents(it->second);
    });
}

void IoThread::postMulticast(const std::vector<uint64_t>& connection_ids, std::string frame) {
    post([this, connection_ids, frame = std::move(frame)]() mutable {
        for (const uint64_t connection_id : connection_ids) {
            auto it = connections_by_id_.find(connection_id);
            if (it == connections_by_id_.end() || it->second->closed()) {
                continue;
            }
            if (!it->second->enqueue(frame)) {
                metrics().broadcast_dropped.fetch_add(1, std::memory_order_relaxed);
                closeConnection(it->second, "write buffer full");
                continue;
            }
            updateEvents(it->second);
        }
    });
}

void IoThread::sendFrame(const Connection::Ptr& connection, std::string frame) {
    if (!running_ || !connection || connection->closed()) {
        return;
    }
    if (!connection->enqueue(std::move(frame))) {
        closeConnection(connection, "write buffer full");
        return;
    }
    updateEvents(connection);
}

void IoThread::handleTasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        tasks.swap(pending_tasks_);
    }
    for (auto& task : tasks) {
        if (task) {
            task();
        }
    }
}

int IoThread::id() const {
    return id_;
}

void IoThread::wakeup() {
    if (event_fd_ < 0) {
        return;
    }
    const uint64_t value = 1;
    const ssize_t written = ::write(event_fd_, &value, sizeof(value));
    (void)written;
}

void IoThread::run() {
    logMessage(LogLevel::kDebug, "io", "thread " + std::to_string(id_) + " loop entered");
    std::vector<epoll_event> events(kMaxEvents);
    while (running_) {
        const int ready = ::epoll_wait(epoll_fd_, events.data(), kMaxEvents, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            logMessage(LogLevel::kError, "io",
                       "thread " + std::to_string(id_) + " epoll_wait failed: " +
                           std::strerror(errno));
            break;
        }

        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd;
            const uint32_t flags = events[i].events;
            if (fd == event_fd_) {
                uint64_t value = 0;
                while (::read(event_fd_, &value, sizeof(value)) > 0) {
                }
                handlePending();
                handleTasks();
                continue;
            }
            if (fd == timer_fd_) {
                uint64_t expirations = 0;
                while (::read(timer_fd_, &expirations, sizeof(expirations)) > 0) {
                }
                handleTimer();
                continue;
            }

            auto it = connections_.find(fd);
            if (it == connections_.end()) {
                continue;
            }
            const Connection::Ptr connection = it->second;

            if ((flags & (EPOLLERR | EPOLLHUP)) != 0) {
                closeConnection(connection, "epoll error/hup");
                continue;
            }
            if ((flags & EPOLLIN) != 0) {
                handleRead(connection);
            }
            if (connection->closed()) {
                continue;
            }
            if ((flags & EPOLLRDHUP) != 0) {
                closeConnection(connection, "peer half closed");
                continue;
            }
            if ((flags & EPOLLOUT) != 0) {
                handleWrite(connection);
            }
        }
    }

    closeAll();
    logMessage(LogLevel::kDebug, "io", "thread " + std::to_string(id_) + " loop exited");
}

void IoThread::handlePending() {
    std::vector<PendingConnection> pending;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending.swap(pending_);
    }

    for (const PendingConnection& item : pending) {
        if (!running_) {
            ::close(item.fd);
            continue;
        }
        auto connection = std::make_shared<Connection>(item.fd, item.id, id_, item.peer_ip,
                                                       proxy_protocol_);
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        event.data.fd = item.fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, item.fd, &event) < 0) {
            logMessage(LogLevel::kWarn, "io",
                       "thread " + std::to_string(id_) + " add fd " +
                           std::to_string(item.fd) + " failed: " + std::strerror(errno));
            connection->close();
            continue;
        }
        connections_.emplace(item.fd, connection);
        connections_by_id_.emplace(item.id, std::move(connection));
        logMessage(LogLevel::kDebug, "io",
                   "thread " + std::to_string(id_) + " accepted conn=" +
                       std::to_string(item.id) + " fd=" + std::to_string(item.fd));
    }
}

void IoThread::handleTimer() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> expired;
    expired.reserve(8);
    for (const auto& entry : connections_) {
        const auto idle = std::chrono::duration_cast<std::chrono::seconds>(
            now - entry.second->lastActive());
        if (idle.count() >= idle_timeout_sec_) {
            expired.push_back(entry.first);
        }
    }
    for (const int fd : expired) {
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            closeConnection(it->second, "idle timeout");
        }
    }
}

void IoThread::handleRead(const Connection::Ptr& connection) {
    char buffer[kReadChunk];
    while (true) {
        const ssize_t received = ::recv(connection->fd(), buffer, sizeof(buffer), 0);
        if (received > 0) {
            connection->input().append(buffer, static_cast<std::size_t>(received));
            connection->touch();
            continue;
        }
        if (received == 0) {
            closeConnection(connection, "peer closed");
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        closeConnection(connection, std::string("recv failed: ") + std::strerror(errno));
        return;
    }

    if (connection->proxyHeaderPending()) {
        std::string proxy_error;
        if (!connection->tryParseProxyHeader(proxy_error)) {
            metrics().protocol_errors.fetch_add(1, std::memory_order_relaxed);
            logMessage(LogLevel::kWarn, "io",
                       "thread " + std::to_string(id_) + " conn=" +
                           std::to_string(connection->id()) + " proxy error: " + proxy_error);
            closeConnection(connection, proxy_error);
            return;
        }
        if (connection->proxyHeaderPending()) {
            return;
        }
    }

    while (!connection->closed()) {
        Frame frame;
        std::string error;
        const FrameParseResult result = extractFrame(connection->input(), frame, error);
        if (result == FrameParseResult::kNeedMore) {
            break;
        }
        if (result == FrameParseResult::kError) {
            metrics().protocol_errors.fetch_add(1, std::memory_order_relaxed);
            logMessage(LogLevel::kWarn, "io",
                       "thread " + std::to_string(id_) + " conn=" +
                           std::to_string(connection->id()) + " protocol error: " + error);
            closeConnection(connection, error);
            return;
        }
        if (!handler_(connection, frame, *this)) {
            closeConnection(connection, "handler requested close");
            return;
        }
        if (connection->closed()) {
            return;
        }
        if (connection->pendingOutputBytes() > 0) {
            updateEvents(connection);
        }
        if (connection->closed()) {
            return;
        }
    }
}

void IoThread::handleWrite(const Connection::Ptr& connection) {
    if (!connection->flush()) {
        closeConnection(connection, std::string("send failed"));
        return;
    }
    if (connection->pendingOutputBytes() == 0) {
        if (connection->closeAfterFlush()) {
            closeConnection(connection, "close after flush");
            return;
        }
        updateEvents(connection);
    }
}

void IoThread::updateEvents(const Connection::Ptr& connection) {
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    if (connection->pendingOutputBytes() > 0) {
        event.events |= EPOLLOUT;
    }
    event.data.fd = connection->fd();
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection->fd(), &event) < 0) {
        logMessage(LogLevel::kWarn, "io",
                   "thread " + std::to_string(id_) + " mod fd " +
                       std::to_string(connection->fd()) + " failed: " + std::strerror(errno));
        closeConnection(connection, "epoll mod failed");
    }
}

void IoThread::closeConnection(const Connection::Ptr& connection, const std::string& reason) {
    if (!connection) {
        return;
    }
    const uint64_t connection_id = connection->id();
    const int fd = connection->fd();
    const bool known = (fd >= 0 && connections_.find(fd) != connections_.end()) ||
                       connections_by_id_.find(connection_id) != connections_by_id_.end();
    if (!known) {
        return;
    }
    if (fd >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    }
    connection->close();
    if (fd >= 0) {
        connections_.erase(fd);
    } else {
        for (auto it = connections_.begin(); it != connections_.end(); ++it) {
            if (it->second == connection) {
                connections_.erase(it);
                break;
            }
        }
    }
    connections_by_id_.erase(connection_id);
    if (close_handler_) {
        close_handler_(connection_id);
    }
    logMessage(LogLevel::kDebug, "io",
               "thread " + std::to_string(id_) + " conn=" + std::to_string(connection_id) +
                   " closed: " + reason);
}

void IoThread::closeAll() {
    for (auto& entry : connections_) {
        const uint64_t connection_id = entry.second->id();
        entry.second->close();
        if (close_handler_) {
            close_handler_(connection_id);
        }
    }
    connections_.clear();
    connections_by_id_.clear();

    std::vector<PendingConnection> pending;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending.swap(pending_);
    }
    for (const PendingConnection& item : pending) {
        ::close(item.fd);
    }
}

}  // namespace chatbox
