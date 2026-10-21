#include "ui/message_action_bar.h"

#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>

MessageActionBar::MessageActionBar(QWidget* parent) : QFrame(parent) {
    setObjectName("MessageActionBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();

    // The Coinbase elevation is one soft drop (0 4px 12px rgba(0,0,0,0.04));
    // QSS has no box-shadow, so the bar carries a single effect instead.
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(12);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 10));
    setGraphicsEffect(shadow);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    reply_button_ = new QPushButton(QString::fromUtf8("回复"), this);
    reply_button_->setObjectName("MessageActionReplyButton");
    reply_button_->setCursor(Qt::PointingHandCursor);
    reply_button_->setFocusPolicy(Qt::TabFocus);
    reply_button_->setAccessibleName(QString::fromUtf8("回复此消息"));
    connect(reply_button_, &QPushButton::clicked, this,
            &MessageActionBar::replyRequested);

    recall_button_ = new QPushButton(QString::fromUtf8("撤回"), this);
    recall_button_->setObjectName("MessageActionRecallButton");
    recall_button_->setCursor(Qt::PointingHandCursor);
    recall_button_->setFocusPolicy(Qt::TabFocus);
    recall_button_->setAccessibleName(QString::fromUtf8("撤回此消息"));
    connect(recall_button_, &QPushButton::clicked, this,
            &MessageActionBar::recallRequested);

    layout->addWidget(reply_button_);
    layout->addWidget(recall_button_);
    QWidget::setTabOrder(reply_button_, recall_button_);
}

void MessageActionBar::setActions(bool can_reply, bool can_recall) {
    reply_button_->setVisible(can_reply);
    recall_button_->setVisible(can_recall);
    adjustSize();
}

void MessageActionBar::leaveEvent(QEvent* event) {
    QFrame::leaveEvent(event);
    // Moving onto a child button also sends a leave event to the frame, so
    // decide asynchronously whether the pointer really left the whole bar.
    QTimer::singleShot(0, this, [this] {
        // Leave events also fire when the pointer moves onto a child button;
        // accept a small corridor around the bar so sliding from the bubble
        // onto the bar does not hide it in between.
        const QRect hit_rect = rect().adjusted(-10, -10, 10, 10);
        if (!hit_rect.contains(mapFromGlobal(QCursor::pos()))) {
            emit hoverLeft();
        }
    });
}
