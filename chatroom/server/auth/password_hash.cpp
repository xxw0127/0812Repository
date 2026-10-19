#include "server/auth/password_hash.h"

#include <argon2.h>
#include <cerrno>
#include <cstddef>
#include <sys/random.h>
#include <vector>

namespace chatroom {

namespace {

constexpr uint32_t kArgon2MemoryKiB = 19 * 1024;
constexpr uint32_t kArgon2Iterations = 2;
constexpr uint32_t kArgon2Parallelism = 1;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

bool fillRandom(unsigned char* buffer, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t received = ::getrandom(buffer + offset, size - offset, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (received == 0) {
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

}  // namespace

bool hashPassword(const std::string& password, std::string& encoded, std::string& error) {
    unsigned char salt[kSaltBytes] = {0};
    if (!fillRandom(salt, sizeof(salt))) {
        error = "failed to generate password salt";
        return false;
    }

    const std::size_t encoded_length = argon2_encodedlen(
        kArgon2Iterations, kArgon2MemoryKiB, kArgon2Parallelism, sizeof(salt), kHashBytes,
        Argon2_id);
    std::vector<char> output(encoded_length, '\0');
    const int result = argon2id_hash_encoded(
        kArgon2Iterations, kArgon2MemoryKiB, kArgon2Parallelism, password.data(),
        password.size(), salt, sizeof(salt), kHashBytes, output.data(), output.size());
    if (result != ARGON2_OK) {
        error = argon2_error_message(result);
        return false;
    }
    encoded.assign(output.data());
    error.clear();
    return true;
}

bool verifyPassword(const std::string& password, const std::string& encoded) {
    if (encoded.empty()) {
        return false;
    }
    return argon2id_verify(encoded.c_str(), password.data(), password.size()) == ARGON2_OK;
}

}  // namespace chatroom
