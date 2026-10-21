#ifndef CHATBOX_CLIENT_CORE_FOCUS_RING_H
#define CHATBOX_CLIENT_CORE_FOCUS_RING_H

#include <QObject>
#include <Qt>

namespace chatbox {
namespace client {

// A focus ring should appear for keyboard navigation, not for a mouse click
// or the window's initial focus. The filter sets the dynamic property
// keyboardFocus on focus-in; QSS selectors only draw the ring when it is true.
bool isKeyboardFocusReason(Qt::FocusReason reason);

class FocusRingFilter : public QObject {
public:
    explicit FocusRingFilter(QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_FOCUS_RING_H
