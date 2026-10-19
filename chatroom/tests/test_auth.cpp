#include <string>

#include "server/auth/password_hash.h"
#include "server/auth/token.h"
#include "tests/test_framework.h"

namespace {

int testPasswordHash() {
    std::string encoded;
    std::string error;
    CHECK_TRUE(chatroom::hashPassword("correct-password", encoded, error));
    CHECK_TRUE(error.empty());
    CHECK_TRUE(encoded.rfind("$argon2id$", 0) == 0);
    CHECK_TRUE(chatroom::verifyPassword("correct-password", encoded));
    CHECK_TRUE(!chatroom::verifyPassword("wrong-password", encoded));

    std::string second;
    CHECK_TRUE(chatroom::hashPassword("correct-password", second, error));
    CHECK_TRUE(second != encoded);
    return 0;
}

int testTokenGenerationAndHash() {
    const std::string first = chatroom::generateToken();
    const std::string second = chatroom::generateToken();
    CHECK_EQ(first.size(), 64u);
    CHECK_EQ(second.size(), 64u);
    CHECK_TRUE(first != second);

    const std::string hash = chatroom::hashToken(first);
    CHECK_EQ(hash.size(), 64u);
    CHECK_EQ(chatroom::hashToken(first), hash);
    CHECK_TRUE(chatroom::hashToken(second) != hash);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testPasswordHash();
    failures += testTokenGenerationAndHash();
    if (failures == 0) {
        std::cout << "test_auth: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
