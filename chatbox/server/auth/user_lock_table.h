#ifndef CHATBOX_SERVER_AUTH_USER_LOCK_TABLE_H
#define CHATBOX_SERVER_AUTH_USER_LOCK_TABLE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace chatbox {

class UserLockTable {
public:
    std::mutex& forUser(uint64_t user_id) {
        return locks_[static_cast<std::size_t>(user_id % locks_.size())];
    }

private:
    std::array<std::mutex, 256> locks_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_USER_LOCK_TABLE_H
