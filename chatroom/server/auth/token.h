#ifndef CHATROOM_SERVER_AUTH_TOKEN_H
#define CHATROOM_SERVER_AUTH_TOKEN_H

#include <string>

namespace chatroom {

// Returns 64 lowercase hex characters, or an empty string on RNG failure.
std::string generateToken();

// Returns SHA-256(token) as 64 lowercase hex characters.
std::string hashToken(const std::string& token);

}  // namespace chatroom

#endif  // CHATROOM_SERVER_AUTH_TOKEN_H
