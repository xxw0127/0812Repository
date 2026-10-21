#include "ui/empty_state.h"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;

EmptyState::EmptyState(QWidget* parent) : QWidget(parent) {
    setObjectName("EmptyState");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(6);
    layout->addStretch(1);

    icon_label_ = new QLabel(this);
    icon_label_->setObjectName("EmptyStateIcon");
    icon_label_->setFixedSize(56, 56);
    icon_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon_label_, 0, Qt::AlignHCenter);

    title_label_ = new QLabel(this);
    title_label_->setObjectName("EmptyStateTitle");
    title_label_->setAlignment(Qt::AlignCenter);
    title_label_->setWordWrap(true);
    layout->addWidget(title_label_);

    description_label_ = new QLabel(this);
    description_label_->setObjectName("EmptyStateDescription");
    description_label_->setAlignment(Qt::AlignCenter);
    description_label_->setWordWrap(true);
    description_label_->setMaximumWidth(320);
    layout->addWidget(description_label_, 0, Qt::AlignHCenter);

    action_button_ = new QPushButton(this);
    action_button_->setObjectName("EmptyStateAction");
    action_button_->setFlat(true);
    action_button_->setCursor(Qt::PointingHandCursor);
    action_button_->hide();
    connect(action_button_, &QPushButton::clicked, this,
            &EmptyState::actionTriggered);
    layout->addWidget(action_button_, 0, Qt::AlignHCenter);

    layout->addStretch(1);

    title_label_->setStyleSheet(
        "font-size: 15px; font-weight: 600; color: " +
        QString::fromLatin1(theme::kInk) + ";");
    description_label_->setStyleSheet(
        "font-size: 13px; color: " + QString::fromLatin1(theme::kMuted) + ";");
    action_button_->setStyleSheet(
        "QPushButton#EmptyStateAction { background: transparent; border: none; "
        "color: " + QString::fromLatin1(theme::kAccent) + "; font-size: 13px; "
        "padding: 6px 8px; }"
        "QPushButton#EmptyStateAction:hover { color: " +
        QString::fromLatin1(theme::kAccentHover) + "; }");
}

void EmptyState::setIcon(const QIcon& icon) {
    const QPixmap source = icon.pixmap(56, 56);
    if (source.isNull()) {
        icon_label_->clear();
        return;
    }
    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);
    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), QColor(theme::kMutedSoft));
    painter.end();
    icon_label_->setPixmap(tinted);
}

void EmptyState::setTitle(const QString& text) {
    title_label_->setText(text);
    title_label_->setVisible(!text.isEmpty());
}

void EmptyState::setDescription(const QString& text) {
    description_label_->setText(text);
    description_label_->setVisible(!text.isEmpty());
}

void EmptyState::setAction(const QString& text) {
    action_button_->setText(text);
    action_button_->setVisible(!text.isEmpty());
}
