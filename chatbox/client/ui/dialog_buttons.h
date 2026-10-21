#ifndef CHATBOX_CLIENT_UI_DIALOG_BUTTONS_H
#define CHATBOX_CLIENT_UI_DIALOG_BUTTONS_H

#include <QMessageBox>
#include <QPushButton>
#include <QString>

#include "core/ui_theme.h"

namespace chatbox {
namespace client {

// Confirmation dialog for destructive actions: the cancel button is the
// default and the accept label is explicit Chinese copy.
inline bool confirmDanger(QWidget* parent, const QString& title,
                          const QString& text, const QString& accept_text,
                          const QString& cancel_text = QStringLiteral("取消")) {
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton,
                    parent);
    QPushButton* accept =
        box.addButton(accept_text, QMessageBox::DestructiveRole);
    QPushButton* cancel =
        box.addButton(cancel_text, QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    accept->setStyleSheet("color: " +
                          QString::fromLatin1(theme::kDangerText) + ";");
    box.exec();
    return box.clickedButton() == accept;
}

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_UI_DIALOG_BUTTONS_H
