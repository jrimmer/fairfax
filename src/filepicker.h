#pragma once

// Open and save dialogs through xdg-desktop-portal
// (org.freedesktop.portal.FileChooser), so Omarchy's own picker appears and
// the app never shows a Qt file dialog. Each request carries a purpose
// string that comes back with the chosen path.

#include <QObject>
#include <QString>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr);

    Q_INVOKABLE void open(const QString &title, const QString &purpose);
    Q_INVOKABLE void openFolder(const QString &title, const QString &purpose);
    Q_INVOKABLE void save(const QString &title, const QString &suggestedName, const QString &purpose);

signals:
    void picked(const QString &path, const QString &purpose);
    void failed(const QString &message);

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    void request(const QString &method, const QString &title, const QVariantMap &options, const QString &purpose);

    QString m_purpose;
    QString m_handle;
};
