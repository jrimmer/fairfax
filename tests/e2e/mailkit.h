#pragma once

// Helpers for the Mail to Precrime end-to-end tests: driving Settings > Mail,
// a fake IMAP server to point it at, and a fake HTTP seam for the OAuth calls.

#include <QClipboard>
#include <QDesktopServices>
#include <QJsonObject>

#include "../fakeimap.h"
#include "keyring.h"
#include "../loopback.h"
#include "settingskit.h"

namespace e2e {

inline QString textOf(const QString &name) {
    QQuickItem *i = findItem(name);
    return i ? i->property("text").toString() : QString("<no such item: " + name + ">");
}

// Opens Settings on the Mail pane and waits until its scrolling area is as tall as its content: the
// pane is long, and a control scrolled to while it is still growing ends up out of view.
inline void openMailPane(App &app, QQuickWindow *&sw) {
    openSettings(app, sw);
    selectPane(sw, "Mail");
    QQuickItem *last = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((last = findItem("mailPipe")) != nullptr, 5000);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (QQuickItem *p = last->parentItem(); p; p = p->parentItem())
            if (p->inherits("QQuickFlickable")) {
                auto *content = p->property("contentItem").value<QQuickItem *>();
                const QRectF r = last->mapRectToItem(content, QRectF(0, 0, last->width(), last->height()));
                return p->property("contentHeight").toReal() >= r.bottom();
            }
        return false;
    }(), 5000);
}

// The index of the entry whose label is `label` in a combo box.
inline int comboIndex(const QString &name, const QString &label) {
    QQuickItem *combo = findItem(name);
    if (!combo)
        return -1;
    for (int i = 0; i < combo->property("count").toInt(); ++i) {
        QString text;
        QMetaObject::invokeMethod(combo, "textAt", Q_RETURN_ARG(QString, text), Q_ARG(int, i));
        if (text == label)
            return i;
    }
    return -1;
}

inline void choose(const QString &name, const QString &label) {
    QQuickItem *combo = nullptr;
    control(name, combo);
    const int index = comboIndex(name, label);
    QVERIFY2(index >= 0, qPrintable(name + " has no entry " + label));
    chooseCombo(name, index);
}

inline QString comboText(const QString &name) {
    QQuickItem *c = findItem(name);
    return c ? c->property("currentText").toString() : QString("<no such combo: " + name + ">");
}

// Empties a text field the way a user does (select all, delete, Return). A field whose empty
// value shows a default (the folder) does not read back as empty, so nothing waits for that.
inline void clearField(QQuickWindow *sw, const QString &name) {
    QQuickItem *field = nullptr;
    control(name, field);
    click(field);
    QTest::keyClick(sw, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(sw, Qt::Key_Delete);
    QTest::keyClick(sw, Qt::Key_Return);
}

// Clicks Fetch Now once the pane has stopped moving (a click that lands on a pane still laying itself out is lost).
inline void clickFetchNow() {
    QQuickItem *button = nullptr;
    control("mailFetchNow", button);
    QVERIFY(button);
    click(button);
}

// Types a password into the masked field and commits it; the field then empties itself.
inline void typePassword(QQuickWindow *sw, const QString &password) {
    QQuickItem *field = nullptr;
    control("mailPassword", field);
    QVERIFY(field);
    click(field);
    typeInto(sw, password);
    QTest::keyClick(sw, Qt::Key_Return);
}

inline QByteArray plusMail(const QString &subject, const QString &id, const QString &to = "jane+precrime@example.org") {
    return FakeImap::mail(to, subject, "Body of " + subject, id);
}

inline QString writeFile(const QString &dir, const QString &name, const QString &body) {
    QFile f(dir + "/" + name);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(body.toUtf8());
    return f.fileName();
}

// A Google "Desktop app" client file, as downloaded from Google Cloud (made up; Precrime ships none).
// Plain string literals: moc loses its place at a raw string.
inline const char *GoogleClient() {
    return "{\"installed\":{\"client_id\":\"e2e-client.apps.example\",\"client_secret\":\"e2e-secret\","
           "\"auth_uri\":\"https://accounts.google.com/o/oauth2/auth\",\"token_uri\":\"https://oauth2.googleapis.com/token\"}}";
}

struct Http {
    QList<QUrl> urls;
    QList<QUrlQuery> forms;
    QJsonObject reply{{"access_token", "e2e-at"}, {"refresh_token", "e2e-rt"}, {"expires_in", 3600}};
};

inline void fakeHttp(App &app, Http &http) {
    app.services().mail->setHttpTransport([&http](const QUrl &url, const QUrlQuery &form, std::function<void(int, const QJsonObject &)> done) {
        http.urls << url;
        http.forms << form;
        done(200, http.reply);
    });
}

// Points the pane at the fake server as an Other account, through the controls.
inline void fillServer(QQuickWindow *sw, FakeImap &server, const QString &toFilter) {
    choose("setting:mail.account", "Other");
    setText(sw, "setting:mail.address", "jane@example.org");
    setText(sw, "setting:mail.host", "127.0.0.1");
    setText(sw, "setting:mail.port", QString::number(server.port()));
    choose("setting:mail.security", "None");
    setText(sw, "setting:mail.user", "jane");
    typePassword(sw, "app-password");
    setText(sw, "setting:mail.toFilter", toFilter);
}


// Waits for the status line to read this (a substring).
inline void waitStatus(const QString &part, int timeout = 8000) {
    QTRY_VERIFY2_WITH_TIMEOUT(textOf("mailStatus").contains(part), qPrintable("status is \"" + textOf("mailStatus") + "\", wanted " + part), timeout);
}

}  // namespace e2e
