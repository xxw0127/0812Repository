#ifndef CHATBOX_SERVER_HISTORY_HISTORY_SERVICE_H
#define CHATBOX_SERVER_HISTORY_HISTORY_SERVICE_H

#include "common/protocol.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

class HistoryService {
public:
    HistoryService(MysqlConnection& database, RoomManager& rooms);

    Frame handle(const Frame& request, const RoomMember& caller);

private:
    Frame handleHistory(const Frame& request, const RoomMember& caller);
    Frame handleSearch(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    RoomManager& rooms_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_HISTORY_HISTORY_SERVICE_H
