#ifndef CHATBOX_CLIENT_CORE_APP_SETTINGS_H
#define CHATBOX_CLIENT_CORE_APP_SETTINGS_H

#include <QSettings>

namespace chatbox {
namespace client {

// One-time migration of the stored settings from the pre-ChatBox identifiers
// (organisation "Chatroom", application "ChatroomClient") to the ChatBox
// identifiers. Copies every key only when the current store is still empty and
// the legacy store has values, then syncs; returns true when values were
// copied. Existing values are never overwritten, so calling it on every start
// is safe.
bool migrateLegacySettings(QSettings& current, QSettings& legacy);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_APP_SETTINGS_H
