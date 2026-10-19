#include <cstddef>
#include <string>

#include "common/text_utils.h"
#include "tests/test_framework.h"

namespace {

int testUtf8CodePointCount() {
    CHECK_EQ(chatbox::utf8CodePointCount("abc"), std::size_t(3));
    CHECK_EQ(chatbox::utf8CodePointCount("你好"), std::size_t(2));
    CHECK_EQ(chatbox::utf8CodePointCount("A😀"), std::size_t(2));
    return 0;
}

int testTrimAsciiWhitespace() {
    CHECK_EQ(chatbox::trimAsciiWhitespace("  hi\n"), std::string("hi"));
    return 0;
}

int testContainsControlCharacters() {
    CHECK_TRUE(chatbox::containsControlCharacters(std::string("a\nb")));
    CHECK_TRUE(!chatbox::containsControlCharacters("你好"));
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testUtf8CodePointCount();
    failures += testTrimAsciiWhitespace();
    failures += testContainsControlCharacters();
    if (failures == 0) {
        std::cout << "test_text_utils: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
