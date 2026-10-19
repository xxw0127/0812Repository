#ifndef CHATROOM_CLIENT_CORE_CONTACT_STORE_H
#define CHATROOM_CLIENT_CORE_CONTACT_STORE_H

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace chatroom {
namespace client {

struct Contact {
    quint64 user_id = 0;
    QString username;
    QString nickname;
};

class ContactStore {
public:
    explicit ContactStore(const QString& scope = "default");

    void setScope(const QString& scope);
    QString scope() const;

    QVector<Contact> contacts() const;
    bool add(const Contact& contact);
    bool remove(quint64 user_id);

private:
    QString scope_;
};

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_CONTACT_STORE_H
