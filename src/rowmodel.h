#pragma once

// A flat list model whose rows are QVariantMaps with a unique "key". Every
// pane (sidebar, task list, tags) is computed as a fresh row list after each
// change and handed to setRows(), which diffs by key and emits fine-grained
// inserts, moves, removes and data changes. That keeps ListView scroll
// positions, animations and focus intact while the logic stays "recompute
// everything", which is simple and fast enough for a personal task database.

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class RowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit RowModel(const QStringList &roles, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }
    const QVector<QVariantMap> &rows() const { return m_rows; }
    void setRows(QVector<QVariantMap> rows);

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfKey(const QString &key) const;

signals:
    void countChanged();

private:
    QStringList m_roles;
    QVector<QVariantMap> m_rows;
};
