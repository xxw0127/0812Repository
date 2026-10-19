#ifndef CHATBOX_SERVER_AUTH_PASSWORD_HASH_H
#define CHATBOX_SERVER_AUTH_PASSWORD_HASH_H

#include <string>

namespace chatbox {

bool hashPassword(const std::string& password, std::string& encoded, std::string& error);
bool verifyPassword(const std::string& password, const std::string& encoded);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_PASSWORD_HASH_H
