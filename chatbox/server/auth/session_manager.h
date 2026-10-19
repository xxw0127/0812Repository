#ifndef CHATBOX_SERVER_AUTH_SESSION_MANAGER_H
#define CHATBOX_SERVER_AUTH_SESSION_MANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "server/net/connection.h"

namespace chatbox {

struct SessionBinding {
    uint64_t connection_id = 0;
    int io_thread_id = -1;
    std::string peer_ip;
    std::weak_ptr<Connection> connection;
};

class SessionManager {
public:
    // Replaces the old binding for the user. Returns true when an old binding
    // with a different connection was present.
    bool bind(uint64_t user_id, const SessionBinding& binding, SessionBinding& old_binding);
    void unbind(uint64_t user_id, uint64_t connection_id);
    void unbindByConnection(uint64_t connection_id);
    bool find(uint64_t user_id, SessionBinding& binding) const;
    bool isOnline(uint64_t user_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, SessionBinding> bindings_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_SESSION_MANAGER_H
