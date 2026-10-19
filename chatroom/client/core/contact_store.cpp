#include "core/contact_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace chatroom {
namespace client {

namespace {

QString contactsKey(const QString& scope) {
    return "contacts/" + scope + "/json";
}

bool loadContacts(const QString& scope, QJsonArray& out) {
    out = QJsonArray{};
    QSettings settings;
    const QByteArray raw = settings.value(contactsKey(scope)).toByteArray();
    if (raw.isEmpty()) {
        return true;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return false;
    }
    out = document.array();
    return true;
}

bool saveContacts(const QString& scope, const QJsonArray& contacts) {
    QSettings settings;
    settings.setValue(contactsKey(scope),
                      QJsonDocument(contacts).toJson(QJsonDocument::Compact));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

Contact contactFromJson(const QJsonObject& object) {
    Contact contact;
    contact.user_id = object.value("user_id").toString().toULongLong();
    contact.username = object.value("username").toString();
    contact.nickname = object.value("nickname").toString();
    return contact;
}

QJsonObject contactToJson(const Contact& contact) {
    QJsonObject object;
    object.insert("user_id", QString::number(contact.user_id));
    object.insert("username", contact.username);
    object.insert("nickname", contact.nickname);
    return object;
}

}  // namespace

ContactStore::ContactStore(const QString& scope) : scope_(scope) {}

void ContactStore::setScope(const QString& scope) {
    scope_ = scope;
}

QString ContactStore::scope() const {
    return scope_;
}

QVector<Contact> ContactStore::contacts() const {
    QVector<Contact> result;
    QJsonArray array;
    if (!loadContacts(scope_, array)) {
        return result;
    }
    result.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const Contact contact = contactFromJson(value.toObject());
        if (contact.user_id != 0) {
            result.push_back(contact);
        }
    }
    return result;
}

bool ContactStore::add(const Contact& contact) {
    if (contact.user_id == 0 || contact.username.isEmpty()) {
        return false;
    }
    QJsonArray array;
    if (!loadContacts(scope_, array)) {
        return false;
    }
    for (int i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            continue;
        }
        const Contact existing = contactFromJson(array.at(i).toObject());
        if (existing.user_id == contact.user_id) {
            array.replace(i, contactToJson(contact));
            saveContacts(scope_, array);
            return false;
        }
    }
    array.append(contactToJson(contact));
    return saveContacts(scope_, array);
}

bool ContactStore::remove(quint64 user_id) {
    QJsonArray array;
    if (!loadContacts(scope_, array)) {
        return false;
    }
    for (int i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            continue;
        }
        const Contact existing = contactFromJson(array.at(i).toObject());
        if (existing.user_id == user_id) {
            array.removeAt(i);
            return saveContacts(scope_, array);
        }
    }
    return false;
}

}  // namespace client
}  // namespace chatroom
