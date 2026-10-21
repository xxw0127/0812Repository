#include "ui/more_dots.h"

#include <QPainter>
#include <QPixmap>
#include <QRectF>

namespace chatbox::client {

QIcon moreDotsIcon(const QColor& color) {
    const int canvas = 44;
    const int dot = 11;
    const int gap = 4;
    const int total = dot * 3 + gap * 2;

    QPixmap pixmap(canvas, canvas);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal left = (canvas - total) / 2.0;
    const qreal top = (canvas - dot) / 2.0;
    for (int i = 0; i < 3; ++i) {
        painter.drawEllipse(QRectF(left + i * (dot + gap), top, dot, dot));
    }
    painter.end();
    return QIcon(pixmap);
}

}  // namespace chatbox::client
