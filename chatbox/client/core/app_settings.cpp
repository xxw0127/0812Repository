#include "core/app_settings.h"

#include <QStringList>

namespace chatbox {
namespace client {

bool migrateLegacySettings(QSettings& current, QSettings& legacy) {
    if (!current.allKeys().isEmpty()) {
        return false;
    }
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty()) {
        return false;
    }
    for (const QString& key : keys) {
        current.setValue(key, legacy.value(key));
    }
    current.sync();
    return true;
}

}  // namespace client
}  // namespace chatbox
