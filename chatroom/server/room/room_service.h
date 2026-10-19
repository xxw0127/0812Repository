#ifndef CHATROOM_SERVER_ROOM_ROOM_SERVICE_H
#define CHATROOM_SERVER_ROOM_ROOM_SERVICE_H

#include <string>
#include <utility>
#include <vector>

#include "common/protocol.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatroom {

struct RoomResult {
    Frame response;
    bool broadcast = false;
    std::string broadcast_frame;
    std::vector<RoomMember> targets;

    RoomResult() = default;
    explicit RoomResult(Frame frame) : response(std::move(frame)) {}
};

class RoomService {
public:
    RoomService(MysqlConnection& database, RoomManager& rooms);

    RoomResult handle(const Frame& request, const RoomMember& caller);

private:
    RoomResult handleRoomList(const Frame& request);
    RoomResult handleJoinRoom(const Frame& request, const RoomMember& caller);
    RoomResult handleLeaveRoom(const Frame& request, const RoomMember& caller);
    RoomResult handleRoomMembers(const Frame& request, const RoomMember& caller);
    RoomResult handleChatSend(const Frame& request, const RoomMember& caller);
    RoomResult handleRecall(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    RoomManager& rooms_;
};

}  // namespace chatroom

#endif  // CHATROOM_SERVER_ROOM_ROOM_SERVICE_H
