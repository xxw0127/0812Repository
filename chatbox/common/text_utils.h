#ifndef CHATROOM_COMMON_TEXT_UTILS_H
#define CHATROOM_COMMON_TEXT_UTILS_H

#include <cstddef>
#include <string>

namespace chatroom {

// Counts UTF-8 code points by counting bytes that are not continuation bytes.
std::size_t utf8CodePointCount(const std::string& value);

// Removes leading and trailing ASCII space, tab, carriage return and newline.
std::string trimAsciiWhitespace(const std::string& value);

// Returns true when the value contains an ASCII control byte (0x00-0x1F or 0x7F).
bool containsControlCharacters(const std::string& value);

}  // namespace chatroom

#endif  // CHATROOM_COMMON_TEXT_UTILS_H
