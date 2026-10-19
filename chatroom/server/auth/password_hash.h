#ifndef CHATROOM_SERVER_AUTH_PASSWORD_HASH_H
#define CHATROOM_SERVER_AUTH_PASSWORD_HASH_H

#include <string>

namespace chatroom {

bool hashPassword(const std::string& password, std::string& encoded, std::string& error);
bool verifyPassword(const std::string& password, const std::string& encoded);

}  // namespace chatroom

#endif  // CHATROOM_SERVER_AUTH_PASSWORD_HASH_H
