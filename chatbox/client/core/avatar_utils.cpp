#include "core/avatar_utils.h"

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QVector>

#include <cmath>

#include "core/ui_theme.h"

namespace chatbox {
namespace client {

namespace {

// WCAG relative luminance. Kept here rather than in a header because the only
// consumers are the avatar painters in this file and readableForeground().
double relativeLuminance(const QColor& color) {
    auto channel = [](double value) {
        value /= 255.0;
        return value <= 0.03928 ? value / 12.92
                                : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) +
           0.0722 * channel(color.blue());
}

// A fixed eight-colour set. These are literals on purpose: they used to start
// with theme::kAccent inside a `static` array, which froze the first entry to
// whichever theme happened to be active on the first call.
const QColor* fallbackPalette(int* count) {
    static const QColor colors[] = {
        QColor("#0052FF"), QColor("#5AD8A6"), QColor("#F6BD16"),
        QColor("#E8684A"), QColor("#6DC8EC"), QColor("#9270CA"),
        QColor("#FF9D4D"), QColor("#269A99"),
    };
    *count = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    return colors;
}

}  // namespace

QColor fallbackAvatarBackground(quint64 seed) {
    int count = 0;
    const QColor* colors = fallbackPalette(&count);
    return colors[seed % static_cast<quint64>(count)];
}

QColor readableForeground(const QColor& background) {
    // Luminance at which white drops below 4.5:1:
    // (1.0 + 0.05) / (L + 0.05) = 4.5  =>  L = 0.1833
    return relativeLuminance(background) > 0.1833 ? QColor("#0A0B0D")
                                                  : QColor(Qt::white);
}

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

    const QColor background = fallbackAvatarBackground(seed);

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
    painter.setPen(readableForeground(background));
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
}  // namespace chatbox
