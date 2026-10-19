#include <cstdint>
#include <string>

#include "common/protocol.h"
#include "tests/test_framework.h"

namespace {

int testHeaderRoundTrip() {
    chatroom::Frame frame;
    frame.header.flags = 0x05;
    frame.header.cmd = 0x1234;
    frame.header.seq = 0x01020304;
    frame.body = "hello";

    const std::string encoded = chatroom::encodeFrame(frame);
    CHECK_EQ(encoded.size(), chatroom::kFrameHeaderSize + 5);
    CHECK_EQ(static_cast<uint8_t>(encoded[0]), 0x43);
    CHECK_EQ(static_cast<uint8_t>(encoded[1]), 0x48);
    CHECK_EQ(static_cast<uint8_t>(encoded[2]), 0x01);
    CHECK_EQ(static_cast<uint8_t>(encoded[3]), 0x05);
    CHECK_EQ(static_cast<uint8_t>(encoded[4]), 0x12);
    CHECK_EQ(static_cast<uint8_t>(encoded[5]), 0x34);
    CHECK_EQ(static_cast<uint8_t>(encoded[6]), 0x01);
    CHECK_EQ(static_cast<uint8_t>(encoded[7]), 0x02);
    CHECK_EQ(static_cast<uint8_t>(encoded[8]), 0x03);
    CHECK_EQ(static_cast<uint8_t>(encoded[9]), 0x04);
    CHECK_EQ(static_cast<uint8_t>(encoded[10]), 0x00);
    CHECK_EQ(static_cast<uint8_t>(encoded[11]), 0x00);
    CHECK_EQ(static_cast<uint8_t>(encoded[12]), 0x00);
    CHECK_EQ(static_cast<uint8_t>(encoded[13]), 0x05);
    CHECK_EQ(encoded.substr(chatroom::kFrameHeaderSize), "hello");

    chatroom::FrameHeader header;
    CHECK_TRUE(chatroom::decodeHeader(encoded.data(), chatroom::kFrameHeaderSize, header));
    CHECK_EQ(header.magic, chatroom::kProtocolMagic);
    CHECK_EQ(header.version, chatroom::kProtocolVersion);
    CHECK_EQ(header.flags, 0x05);
    CHECK_EQ(header.cmd, 0x1234);
    CHECK_EQ(header.seq, 0x01020304);
    CHECK_EQ(header.body_len, 5);

    std::string error;
    CHECK_TRUE(chatroom::validateHeader(header, chatroom::kMaxInFrameBody, error));
    CHECK_TRUE(error.empty());
    return 0;
}

int testValidationRejectsBadHeaders() {
    std::string error;
    chatroom::FrameHeader header;

    header.magic = 0x0000;
    CHECK_TRUE(!chatroom::validateHeader(header, chatroom::kMaxInFrameBody, error));
    CHECK_EQ(error, "invalid magic");

    header.magic = chatroom::kProtocolMagic;
    header.version = 2;
    CHECK_TRUE(!chatroom::validateHeader(header, chatroom::kMaxInFrameBody, error));
    CHECK_EQ(error, "unsupported protocol version");

    header.version = chatroom::kProtocolVersion;
    header.body_len = chatroom::kMaxInFrameBody + 1;
    CHECK_TRUE(!chatroom::validateHeader(header, chatroom::kMaxInFrameBody, error));
    CHECK_EQ(error, "frame body too large");

    header.body_len = 0;
    CHECK_TRUE(chatroom::validateHeader(header, chatroom::kMaxInFrameBody, error));
    CHECK_TRUE(error.empty());
    return 0;
}

int testByteReaderWriter() {
    chatroom::ByteWriter writer;
    writer.putU8(0x7F);
    writer.putU16(0x1234);
    writer.putU32(0x01020304);
    writer.putU64(0x0102030405060708ULL);
    writer.putString("abc");
    CHECK_EQ(writer.data().size(), 20);

    chatroom::ByteReader reader(writer.data().data(), writer.data().size());
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    std::string text;
    CHECK_TRUE(reader.getU8(u8));
    CHECK_EQ(u8, 0x7F);
    CHECK_TRUE(reader.getU16(u16));
    CHECK_EQ(u16, 0x1234);
    CHECK_TRUE(reader.getU32(u32));
    CHECK_EQ(u32, 0x01020304u);
    CHECK_TRUE(reader.getU64(u64));
    CHECK_EQ(u64, 0x0102030405060708ULL);
    CHECK_TRUE(reader.getString(text));
    CHECK_EQ(text, "abc");
    CHECK_EQ(reader.remaining(), 0u);
    return 0;
}

int testByteReaderRejectsTruncatedValues() {
    chatroom::ByteWriter writer;
    writer.putString("abcdef");
    const std::string data = writer.data();

    chatroom::ByteReader reader(data.data(), data.size() - 1);
    std::string text;
    CHECK_TRUE(!reader.getString(text));
    CHECK_EQ(reader.remaining(), data.size() - 1);

    chatroom::ByteReader empty_reader(nullptr, 0);
    uint32_t value = 0;
    CHECK_TRUE(!empty_reader.getU32(value));
    CHECK_EQ(empty_reader.remaining(), 0u);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testHeaderRoundTrip();
    failures += testValidationRejectsBadHeaders();
    failures += testByteReaderWriter();
    failures += testByteReaderRejectsTruncatedValues();
    if (failures == 0) {
        std::cout << "test_protocol: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
