#pragma once

// A small zip reader: the central directory, then stored and deflated members
// with their CRC-32 and size checked. Enough to pull a database out of a
// zipped backup; it does not write archives, and it refuses encryption, ZIP64
// and compression methods other than stored and deflate.

#include <QByteArray>
#include <QList>
#include <QString>

namespace ZipArchive {

struct Entry {
    QString name;
    quint16 method = 0;
    quint16 flags = 0;
    quint32 crc = 0;
    quint64 compressedSize = 0;
    quint64 size = 0;
    quint64 localOffset = 0;
    bool isDirectory() const { return name.endsWith(QLatin1Char('/')); }
};

enum class Status { Ok, Corrupt, Unsupported };

struct Listing {
    Status status = Status::Ok;
    QString error;
    QList<Entry> entries;
};

struct Content {
    Status status = Status::Ok;
    QString error;
    QByteArray data;
};

// True when this build can inflate deflated members (zlib is linked).
bool deflateAvailable();

// Reads the central directory of the archive at path.
Listing list(const QString &path);

// Reads a member. With maxBytes > 0 only that many leading bytes are returned
// and the CRC is not checked; otherwise the whole member is read and checked.
Content read(const QString &path, const Entry &entry, qint64 maxBytes = 0);

// The CRC-32 zip uses (also handy for building archives in tests).
quint32 crc32Of(const QByteArray &data);

}  // namespace ZipArchive
