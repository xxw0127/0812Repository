#ifndef CHATROOM_SERVER_NET_BUFFER_H
#define CHATROOM_SERVER_NET_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

#include "common/protocol.h"

namespace chatroom {

class Buffer {
public:
    void append(const char* data, std::size_t size);
    void append(const std::string& data);
    const char* data() const;
    std::size_t size() const;
    bool empty() const;
    void consume(std::size_t size);
    void clear();

private:
    std::vector<char> storage_;
    std::size_t read_index_ = 0;
};

enum class FrameParseResult {
    kNeedMore,
    kOk,
    kError,
};

FrameParseResult extractFrame(Buffer& buffer, Frame& frame, std::string& error);

}  // namespace chatroom

#endif  // CHATROOM_SERVER_NET_BUFFER_H
