#ifndef CHATROOM_CLIENT_CORE_AVATAR_UTILS_H
#define CHATROOM_CLIENT_CORE_AVATAR_UTILS_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QtGlobal>

namespace chatroom {
namespace client {

// Crops the largest centered square out of source.
QImage centerCropSquare(const QImage& source);

// Crops, scales and JPEG-encodes source, trying the sizes/qualities from the
// avatar spec until the result fits max_bytes. Returns an empty array when no
// attempt fits (or when source is null).
QByteArray encodeAvatarJpeg(const QImage& source, int max_bytes = 48 * 1024);

// Draws the first character of name on a colored circle. The color is stable
// for a given seed.
QImage fallbackAvatar(const QString& name, quint64 seed, int size);

// Scales source to size x size and masks it to a circle with transparent
// corners.
QImage circularAvatar(const QImage& source, int size);

// Draws the brand room badge (accent disc + white speech bubble) used for
// rooms in the sidebar and chat header. Geometry: see
// docs/superpowers/specs/2026-10-19-client-brand-icons.md §3.3.
QImage roomAvatar(int size);

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_AVATAR_UTILS_H
