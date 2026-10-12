#include "rowmodel.h"

#include <QHash>
#include <QSet>

RowModel::RowModel(const QStringList &roles, QObject *parent) : QAbstractListModel(parent), m_roles(roles) {
    if (!m_roles.contains("key"))
        m_roles.prepend("key");
    // "row" hands QML the whole row as a plain object. Unlike the per-role
    // properties of a delegate's model object, it survives the delegate's
    // teardown, so bindings never see a destroyed model entry.
    m_roles.append("row");
}

int RowModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant RowModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const int r = role - Qt::UserRole - 1;
    if (r < 0 || r >= m_roles.size())
        return {};
    if (r == m_roles.size() - 1)
        return m_rows.at(index.row());
    return m_rows.at(index.row()).value(m_roles.at(r));
}

QHash<int, QByteArray> RowModel::roleNames() const {
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i)
        names.insert(Qt::UserRole + 1 + i, m_roles.at(i).toUtf8());
    return names;
}

QVariantMap RowModel::get(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap(); }

int RowModel::indexOfKey(const QString &key) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).value("key").toString() == key)
            return i;
    return -1;
}

void RowModel::setRows(QVector<QVariantMap> next) {
    const int oldCount = m_rows.size();

    // Drop rows whose keys are gone, back to front so indices stay valid.
    QSet<QString> nextKeys;
    for (const auto &row : next)
        nextKeys.insert(row.value("key").toString());
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (!nextKeys.contains(m_rows.at(i).value("key").toString())) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }

    // Walk the new order: keep matches in place, move rows that exist further
    // down, and insert the rest.
    for (int i = 0; i < next.size(); ++i) {
        const QString key = next.at(i).value("key").toString();
        if (i < m_rows.size() && m_rows.at(i).value("key").toString() == key) {
            if (m_rows.at(i) != next.at(i)) {
                m_rows[i] = next.at(i);
                emit dataChanged(index(i), index(i));
            }
            continue;
        }
        int found = -1;
        for (int j = i + 1; j < m_rows.size(); ++j) {
            if (m_rows.at(j).value("key").toString() == key) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            beginMoveRows({}, found, found, {}, i);
            m_rows.move(found, i);
            endMoveRows();
            if (m_rows.at(i) != next.at(i)) {
                m_rows[i] = next.at(i);
                emit dataChanged(index(i), index(i));
            }
        } else {
            beginInsertRows({}, i, i);
            m_rows.insert(i, next.at(i));
            endInsertRows();
        }
    }

    if (m_rows.size() != oldCount)
        emit countChanged();
}
