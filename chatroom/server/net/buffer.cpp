#include "server/net/buffer.h"

#include <algorithm>

namespace chatroom {

void Buffer::append(const char* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    storage_.insert(storage_.end(), data, data + size);
}

void Buffer::append(const std::string& data) {
    append(data.data(), data.size());
}

const char* Buffer::data() const {
    if (read_index_ >= storage_.size()) {
        return nullptr;
    }
    return storage_.data() + read_index_;
}

std::size_t Buffer::size() const {
    return storage_.size() - read_index_;
}

bool Buffer::empty() const {
    return size() == 0;
}

void Buffer::consume(std::size_t size) {
    if (size >= this->size()) {
        clear();
        return;
    }
    read_index_ += size;
    if (read_index_ > 8192 && read_index_ * 2 >= storage_.size()) {
        storage_.erase(storage_.begin(), storage_.begin() + static_cast<std::ptrdiff_t>(read_index_));
        read_index_ = 0;
    }
}

void Buffer::clear() {
    storage_.clear();
    read_index_ = 0;
}

FrameParseResult extractFrame(Buffer& buffer, Frame& frame, std::string& error) {
    if (buffer.size() < kFrameHeaderSize) {
        return FrameParseResult::kNeedMore;
    }

    FrameHeader header;
    if (!decodeHeader(buffer.data(), kFrameHeaderSize, header)) {
        error = "incomplete frame header";
        return FrameParseResult::kError;
    }
    if (!validateHeader(header, kMaxInFrameBody, error)) {
        return FrameParseResult::kError;
    }

    const std::size_t total = kFrameHeaderSize + header.body_len;
    if (buffer.size() < total) {
        return FrameParseResult::kNeedMore;
    }

    frame.header = header;
    frame.body.assign(buffer.data() + kFrameHeaderSize, header.body_len);
    buffer.consume(total);
    return FrameParseResult::kOk;
}

}  // namespace chatroom
