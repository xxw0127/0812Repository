#include "core/protocol_qt.h"

#include <string>

namespace chatroom {

QByteArray encodeToByteArray(const Frame& frame) {
    const std::string data = encodeFrame(frame);
    return QByteArray(data.data(), static_cast<int>(data.size()));
}

bool decodeFromByteArray(const QByteArray& data, Frame& frame) {
    if (data.size() < static_cast<int>(kFrameHeaderSize)) {
        return false;
    }
    FrameHeader header;
    if (!decodeHeader(data.constData(), static_cast<std::size_t>(data.size()), header)) {
        return false;
    }
    std::string error;
    if (!validateHeader(header, kMaxInFrameBody, error)) {
        return false;
    }
    const int total = static_cast<int>(kFrameHeaderSize + header.body_len);
    if (data.size() < total) {
        return false;
    }
    frame.header = header;
    frame.body.assign(data.constData() + kFrameHeaderSize, header.body_len);
    return true;
}

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string toUtf8(const QString& value) {
    const QByteArray data = value.toUtf8();
    return std::string(data.constData(), static_cast<std::size_t>(data.size()));
}

}  // namespace chatroom
