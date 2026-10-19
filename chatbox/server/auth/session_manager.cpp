#include "server/auth/session_manager.h"

namespace chatbox {

bool SessionManager::bind(uint64_t user_id, const SessionBinding& binding,
                          SessionBinding& old_binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(user_id);
    if (it == bindings_.end()) {
        bindings_[user_id] = binding;
        return false;
    }

    old_binding = it->second;
    const bool different = old_binding.connection_id != binding.connection_id;
    it->second = binding;
    return different;
}

void SessionManager::unbind(uint64_t user_id, uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(user_id);
    if (it != bindings_.end() && it->second.connection_id == connection_id) {
        bindings_.erase(it);
    }
}

void SessionManager::unbindByConnection(uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = bindings_.begin(); it != bindings_.end();) {
        if (it->second.connection_id == connection_id) {
            it = bindings_.erase(it);
        } else {
            ++it;
        }
    }
}

bool SessionManager::find(uint64_t user_id, SessionBinding& binding) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(user_id);
    if (it == bindings_.end() || it->second.connection.expired()) {
        return false;
    }
    binding = it->second;
    return true;
}

bool SessionManager::isOnline(uint64_t user_id) const {
    SessionBinding binding;
    return find(user_id, binding);
}

}  // namespace chatbox
