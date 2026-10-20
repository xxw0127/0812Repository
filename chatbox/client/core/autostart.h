#ifndef CHATBOX_CLIENT_CORE_AUTOSTART_H
#define CHATBOX_CLIENT_CORE_AUTOSTART_H

#include <QString>

class QSettings;

namespace chatbox {
namespace client {

// Command line stored in HKCU Run: "<exe>" --minimized. The quotes keep paths
// with spaces intact; --minimized makes the app start in the tray.
QString autostartCommand(const QString& exe_path);

// Writes/removes the ChatBox value in the supplied store. Separated from the
// registry lookup so tests can drive it with an INI file.
void applyAutostartSetting(QSettings& run_key, bool enabled, const QString& exe_path);

// Applies the setting to HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
// Returns false when the platform or the store cannot honour it.
bool setAutostartEnabled(bool enabled, const QString& exe_path);
bool isAutostartEnabled();

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_AUTOSTART_H
