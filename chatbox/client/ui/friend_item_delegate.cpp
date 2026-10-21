#include "ui/friend_item_delegate.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

#include "core/avatar_utils.h"
#include "core/message_time.h"
#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kNicknameRole = Qt::UserRole + 1;
constexpr int kUnreadRole = Qt::UserRole + 2;
constexpr int kOnlineRole = Qt::UserRole + 3;
constexpr int kContentRole = Qt::UserRole + 4;
constexpr int kTimeRole = Qt::UserRole + 5;
constexpr int kRemarkRole = Qt::UserRole + 6;

}  // namespace

FriendItemDelegate::FriendItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void FriendItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const quint64 id = index.data(kIdRole).toULongLong();
    if (id == 0) {
        painter->setPen(QColor(theme::kMuted));
        QFont font = painter->font();
        font.setPixelSize(11);
        font.setBold(true);
        painter->setFont(font);
        painter->drawText(option.rect.adjusted(10, 0, -8, 0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    QRect row = option.rect.adjusted(4, 2, -4, -2);
    if (selected) {
        // Mirrors QListWidget::item:selected in style.qss; the delegate paints
        // its own row, so the stylesheet never reaches this list. Telegram
        // fills the whole row with the accent and drops the outline.
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(theme::kSelectionBg));
        painter->drawRoundedRect(row, 8, 8);
    } else if (hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(theme::kSurfaceSoft));
        painter->drawRoundedRect(row, 8, 8);
    }

    const QString nickname = index.data(kNicknameRole).toString();
    const QString remark = index.data(kRemarkRole).toString();
    const QString name = remark.isEmpty() ? nickname : remark;
    const bool online = index.data(kOnlineRole).toBool();
    const int unread = index.data(kUnreadRole).toInt();
    const QString content = index.data(kContentRole).toString();
    const QString time = index.data(kTimeRole).toString();

    const int avatar_size = 36;
    const QRect avatar_rect(row.left() + 8, row.center().y() - avatar_size / 2,
                            avatar_size, avatar_size);
    const QPixmap avatar = index.data(kAvatarRole).value<QPixmap>();
    if (!avatar.isNull()) {
        QPainterPath clip;
        clip.addEllipse(avatar_rect);
        painter->save();
        painter->setClipPath(clip);
        painter->drawPixmap(avatar_rect, avatar);
        painter->restore();
    } else {
        const QColor avatar_bg = chatbox::client::fallbackAvatarBackground(id);
        painter->setPen(Qt::NoPen);
        painter->setBrush(avatar_bg);
        painter->drawEllipse(avatar_rect);
        QFont avatar_font = painter->font();
        avatar_font.setPixelSize(15);
        avatar_font.setBold(true);
        painter->setFont(avatar_font);
        painter->setPen(chatbox::client::readableForeground(avatar_bg));
        painter->drawText(avatar_rect, Qt::AlignCenter,
                          name.isEmpty() ? QString("?") : name.left(1).toUpper());
    }

    if (online) {
        painter->setBrush(QColor(theme::kOnlineDot));
        painter->setPen(QPen(QColor(theme::kCanvas), 2));
        painter->drawEllipse(QPoint(avatar_rect.right() - 2, avatar_rect.bottom() - 2),
                             5, 5);
    }

    const int text_left = avatar_rect.right() + 10;
    const int text_right = row.right() - 8;
    // The selected row is accent-filled, so every foreground flips to white;
    // the preview and timestamp keep hierarchy through alpha, not grey.
    const QColor primary =
        selected ? QColor(theme::kOnPrimary) : QColor(theme::kInk);
    QColor secondary(theme::kMuted);
    if (selected) {
        secondary = QColor(theme::kOnPrimary);
        secondary.setAlpha(190);
    }

    QFont name_font = painter->font();
    name_font.setPixelSize(13);
    name_font.setBold(true);
    painter->setFont(name_font);
    painter->setPen(primary);
    const QRect name_rect(text_left, row.top() + 11, text_right - text_left - 48, 18);
    painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(name_font).elidedText(name, Qt::ElideRight,
                                                         name_rect.width()));

    QFont preview_font = painter->font();
    preview_font.setPixelSize(11);
    preview_font.setBold(false);
    painter->setFont(preview_font);
    painter->setPen(secondary);
    const QString preview = content.isEmpty() ? QString("暂无消息") : content;
    const QRect preview_rect(text_left, row.top() + 33, text_right - text_left - 38, 16);
    painter->drawText(preview_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(preview_font).elidedText(preview, Qt::ElideRight,
                                                            preview_rect.width()));

    if (!time.isEmpty()) {
        const QDateTime utc = chatbox::client::parseServerUtc(time);
        const QString local =
            utc.isValid() ? chatbox::client::formatLocalTime(utc) : time;
        QFont time_font = painter->font();
        time_font.setPixelSize(11);
        painter->setFont(time_font);
        painter->setPen(secondary);
        painter->drawText(QRect(row.right() - 54, row.top() + 11, 46, 16),
                          Qt::AlignRight | Qt::AlignVCenter, local);
    }

    if (unread > 0) {
        const QString label = unread > 99 ? QString("99+") : QString::number(unread);
        QFont badge_font = painter->font();
        badge_font.setPixelSize(11);
        badge_font.setBold(true);
        painter->setFont(badge_font);
        const int width = qMax(18, QFontMetrics(badge_font).horizontalAdvance(label) + 10);
        const QRect badge(row.right() - width - 6, row.bottom() - 22, width, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? QColor(theme::kOnPrimary)
                                   : QColor(theme::kAccent));
        painter->drawRoundedRect(badge, 8, 8);
        painter->setPen(selected ? QColor(theme::kAccent)
                                 : QColor(theme::kOnPrimary));
        painter->drawText(badge, Qt::AlignCenter, label);
    }

    painter->restore();
}

QSize FriendItemDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const {
    return index.data(kIdRole).toULongLong() == 0 ? QSize(0, 28) : QSize(0, 64);
}
