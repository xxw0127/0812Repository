#include "common/text_utils.h"

namespace chatbox {

namespace {

constexpr const char* kAsciiWhitespace = " \t\r\n";

}  // namespace

std::size_t utf8CodePointCount(const std::string& value) {
    std::size_t count = 0;
    for (const unsigned char byte : value) {
        if ((byte & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

std::string trimAsciiWhitespace(const std::string& value) {
    const std::size_t first = value.find_first_not_of(kAsciiWhitespace);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(kAsciiWhitespace);
    return value.substr(first, last - first + 1);
}

bool containsControlCharacters(const std::string& value) {
    for (const unsigned char byte : value) {
        if (byte < 0x20 || byte == 0x7F) {
            return true;
        }
    }
    return false;
}

}  // namespace chatbox
