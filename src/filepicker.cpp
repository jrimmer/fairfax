#include "filepicker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QUrl>
#include <QUuid>

namespace {

constexpr auto Portal = "org.freedesktop.portal.Desktop";
constexpr auto PortalPath = "/org/freedesktop/portal/desktop";
constexpr auto Chooser = "org.freedesktop.portal.FileChooser";

}  // namespace

FilePicker::FilePicker(QObject *parent) : QObject(parent) {}

void FilePicker::open(const QString &title, const QString &purpose) {
    request("OpenFile", title, {}, purpose);
}

void FilePicker::save(const QString &title, const QString &suggestedName, const QString &purpose) {
    request("SaveFile", title, {{"current_name", suggestedName}}, purpose);
}

void FilePicker::request(const QString &method, const QString &title, const QVariantMap &extra, const QString &purpose) {
    auto bus = QDBusConnection::sessionBus();
    // The portal answers on a Request object whose path we can predict from
    // our unique bus name and a handle token, so we subscribe before calling
    // and can't miss a fast reply.
    const QString token = "precrime" + QUuid::createUuid().toString(QUuid::Id128).left(12);
    QString sender = bus.baseService().mid(1).replace('.', '_');
    const QString handle = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    if (!m_handle.isEmpty())
        bus.disconnect(Portal, m_handle, "org.freedesktop.portal.Request", "Response", this,
                       SLOT(onResponse(uint, QVariantMap)));
    m_handle = handle;
    m_purpose = purpose;
    bus.connect(Portal, handle, "org.freedesktop.portal.Request", "Response", this, SLOT(onResponse(uint, QVariantMap)));

    QVariantMap options = extra;
    options.insert("handle_token", token);
    options.insert("modal", true);
    QDBusMessage call = QDBusMessage::createMethodCall(Portal, PortalPath, Chooser, method);
    call << QString() << title << options;
    const QDBusReply<QDBusObjectPath> reply = bus.call(call);
    if (!reply.isValid())
        emit failed("The file chooser isn't available (xdg-desktop-portal): " + reply.error().message());
}

void FilePicker::onResponse(uint response, const QVariantMap &results) {
    if (response != 0)
        return;  // Cancelled.
    const QStringList uris = results.value("uris").toStringList();
    if (uris.isEmpty())
        return;
    emit picked(QUrl(uris.first()).toLocalFile(), m_purpose);
}
