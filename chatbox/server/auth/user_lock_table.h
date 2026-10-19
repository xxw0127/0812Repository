#ifndef CHATROOM_SERVER_AUTH_USER_LOCK_TABLE_H
#define CHATROOM_SERVER_AUTH_USER_LOCK_TABLE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace chatroom {

class UserLockTable {
public:
    std::mutex& forUser(uint64_t user_id) {
        return locks_[static_cast<std::size_t>(user_id % locks_.size())];
    }

private:
    std::array<std::mutex, 256> locks_;
};

}  // namespace chatroom

#endif  // CHATROOM_SERVER_AUTH_USER_LOCK_TABLE_H
