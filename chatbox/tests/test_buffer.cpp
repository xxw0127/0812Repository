#include <string>

#include "server/net/buffer.h"
#include "tests/test_framework.h"

namespace {

int testAppendAndConsume() {
    chatroom::Buffer buffer;
    buffer.append("hello", 5);
    CHECK_EQ(buffer.size(), 5u);
    CHECK_TRUE(!buffer.empty());
    CHECK_EQ(std::string(buffer.data(), buffer.size()), "hello");

    buffer.consume(2);
    CHECK_EQ(buffer.size(), 3u);
    CHECK_EQ(std::string(buffer.data(), buffer.size()), "llo");

    buffer.consume(3);
    CHECK_TRUE(buffer.empty());
    CHECK_TRUE(buffer.data() == nullptr);
    return 0;
}

int testHalfFrameWaitsForMore() {
    chatroom::Frame frame;
    frame.header.cmd = chatroom::kCmdPing;
    frame.header.seq = 7;
    frame.body = "hello";
    const std::string encoded = chatroom::encodeFrame(frame);

    chatroom::Buffer buffer;
    buffer.append(encoded.data(), 5);

    chatroom::Frame parsed;
    std::string error;
    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kNeedMore);
    CHECK_EQ(buffer.size(), 5u);

    buffer.append(encoded.data() + 5, encoded.size() - 5);
    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kOk);
    CHECK_EQ(parsed.header.seq, 7u);
    CHECK_EQ(parsed.body, "hello");
    CHECK_TRUE(buffer.empty());
    return 0;
}

int testCoalescedFrames() {
    chatroom::Frame first;
    first.header.cmd = chatroom::kCmdPing;
    first.header.seq = 1;
    first.body = "one";

    chatroom::Frame second;
    second.header.cmd = chatroom::kCmdPing;
    second.header.seq = 2;
    second.body = "two";

    const std::string encoded = chatroom::encodeFrame(first) + chatroom::encodeFrame(second);
    chatroom::Buffer buffer;
    buffer.append(encoded);

    chatroom::Frame parsed;
    std::string error;
    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kOk);
    CHECK_EQ(parsed.header.seq, 1u);
    CHECK_EQ(parsed.body, "one");

    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kOk);
    CHECK_EQ(parsed.header.seq, 2u);
    CHECK_EQ(parsed.body, "two");

    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kNeedMore);
    return 0;
}

int testInvalidMagicIsError() {
    chatroom::Frame frame;
    frame.header.cmd = chatroom::kCmdPing;
    frame.body = "x";
    std::string encoded = chatroom::encodeFrame(frame);
    encoded[0] = '\0';

    chatroom::Buffer buffer;
    buffer.append(encoded);
    chatroom::Frame parsed;
    std::string error;
    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kError);
    CHECK_TRUE(!error.empty());
    return 0;
}

int testOversizedBodyIsRejectedWithoutAllocation() {
    chatroom::Frame frame;
    frame.header.cmd = chatroom::kCmdPing;
    std::string encoded = chatroom::encodeFrame(frame);
    encoded[10] = 0x00;
    encoded[11] = 0x01;
    encoded[12] = 0x00;
    encoded[13] = 0x01;  // 65537, just above kMaxInFrameBody

    chatroom::Buffer buffer;
    buffer.append(encoded);
    chatroom::Frame parsed;
    std::string error;
    CHECK_EQ(chatroom::extractFrame(buffer, parsed, error), chatroom::FrameParseResult::kError);
    CHECK_EQ(error, "frame body too large");
    return 0;
}

int testCompactionKeepsData() {
    chatroom::Buffer buffer;
    buffer.append(std::string(20000, 'a'));
    buffer.consume(15000);
    buffer.append(std::string(1000, 'b'));
    CHECK_EQ(buffer.size(), 6000u);
    CHECK_EQ(buffer.data()[0], 'a');
    CHECK_EQ(buffer.data()[4999], 'a');
    CHECK_EQ(buffer.data()[5000], 'b');
    CHECK_EQ(buffer.data()[5999], 'b');
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testAppendAndConsume();
    failures += testHalfFrameWaitsForMore();
    failures += testCoalescedFrames();
    failures += testInvalidMagicIsError();
    failures += testOversizedBodyIsRejectedWithoutAllocation();
    failures += testCompactionKeepsData();
    if (failures == 0) {
        std::cout << "test_buffer: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
