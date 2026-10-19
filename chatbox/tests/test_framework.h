#ifndef CHATROOM_TESTS_TEST_FRAMEWORK_H
#define CHATROOM_TESTS_TEST_FRAMEWORK_H

#include <iostream>
#include <string>

#define CHECK_TRUE(condition)                                                          \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_TRUE(" #condition     \
                      << ") failed\n";                                                 \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        if (!((actual) == (expected))) {                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #actual  \
                      << " == " #expected << "\n";                                     \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

#endif  // CHATROOM_TESTS_TEST_FRAMEWORK_H
