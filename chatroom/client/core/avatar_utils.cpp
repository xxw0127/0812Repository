#include "core/avatar_utils.h"

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QVector>

#include "core/ui_theme.h"

namespace chatroom {
namespace client {

QImage centerCropSquare(const QImage& source) {
    if (source.isNull()) {
        return QImage();
    }
    const int side = qMin(source.width(), source.height());
    if (side <= 0) {
        return QImage();
    }
    const int x = (source.width() - side) / 2;
    const int y = (source.height() - side) / 2;
    return source.copy(x, y, side, side);
}

QByteArray encodeAvatarJpeg(const QImage& source, int max_bytes) {
    if (source.isNull() || max_bytes <= 0) {
        return QByteArray();
    }

    const QImage square = centerCropSquare(source);
    if (square.isNull()) {
        return QByteArray();
    }

    // Spec ladder: start at 256x256/quality 85, lower quality to 75 and 65,
    // then lower the size to 192 and 128, until the JPEG fits the limit.
    static const int sizes[] = {256, 192, 128};
    static const int qualities[] = {85, 75, 65};
    for (const int size : sizes) {
        const QImage scaled = square.scaled(size, size, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
        for (const int quality : qualities) {
            QByteArray data;
            QBuffer buffer(&data);
            if (!buffer.open(QIODevice::WriteOnly)) {
                continue;
            }
            if (scaled.save(&buffer, "JPEG", quality) && data.size() <= max_bytes) {
                return data;
            }
        }
    }
    return QByteArray();
}

QImage fallbackAvatar(const QString& name, quint64 seed, int size) {
    if (size <= 0) {
        return QImage();
    }

    static const QColor colors[] = {
        QColor(theme::kAccent), QColor("#5AD8A6"), QColor("#F6BD16"),
        QColor("#E8684A"), QColor("#6DC8EC"), QColor("#9270CA"),
        QColor("#FF9D4D"), QColor("#269A99")
    };
    const int color_count = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    const QColor background = colors[seed % static_cast<quint64>(color_count)];

    QString initial;
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty()) {
        const QVector<uint> code_points = trimmed.toUcs4();
        if (!code_points.isEmpty()) {
            initial = QString::fromUcs4(code_points.constData(), 1);
        }
    }
    if (initial.isEmpty()) {
        initial = QStringLiteral("?");
    }
    initial = initial.toUpper();

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(QRectF(0, 0, size, size));

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(8, size / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initial);
    painter.end();

    return image;
}

QImage circularAvatar(const QImage& source, int size) {
    if (source.isNull() || size <= 0) {
        return QImage();
    }

    const QImage square = centerCropSquare(source);
    const QImage scaled = square.scaled(size, size, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation);

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clip);
    painter.drawImage(QRect(0, 0, size, size), scaled);
    painter.end();

    return image;
}

QImage roomAvatar(int size) {
    if (size <= 0) {
        return QImage();
    }

    // Geometry lives in a 256-unit design space (spec §3.3) and is scaled down
    // to the requested pixel size.
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size / 256.0, size / 256.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(theme::kAccent));
    painter.drawEllipse(QRectF(0, 0, 256, 256));

    QPainterPath bubble;
    bubble.addRoundedRect(QRectF(58, 62, 140, 122), 46, 46);
    painter.setBrush(Qt::white);
    painter.drawPath(bubble);

    QPainterPath tail;
    tail.moveTo(88, 172);
    tail.lineTo(74, 212);
    tail.lineTo(130, 178);
    tail.closeSubpath();
    painter.setPen(QPen(Qt::white, 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(tail);
    painter.end();

    return image;
}

}  // namespace client
}  // namespace chatroom
