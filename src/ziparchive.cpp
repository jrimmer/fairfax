#include "ziparchive.h"

#include <QFile>

#include <array>

#ifndef FAIRFAX_NO_ZLIB
#include <zlib.h>
#endif

namespace ZipArchive {

namespace {

constexpr quint32 kEndSig = 0x06054b50;
constexpr quint32 kZip64LocatorSig = 0x07064b50;
constexpr quint32 kCentralSig = 0x02014b50;
constexpr quint32 kLocalSig = 0x04034b50;
constexpr qint64 kMaxMember = qint64(1) << 31;  // A QByteArray cannot hold more.

quint16 u16(const QByteArray &b, qsizetype at) {
    return quint16(quint8(b[at]) | (quint8(b[at + 1]) << 8));
}

quint32 u32(const QByteArray &b, qsizetype at) {
    return quint32(u16(b, at)) | (quint32(u16(b, at + 2)) << 16);
}

QString damaged() {
    return QStringLiteral("This backup is damaged or is not a zip archive.");
}

}  // namespace

quint32 crc32Of(const QByteArray &data) {
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data)
        c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool deflateAvailable() {
#ifndef FAIRFAX_NO_ZLIB
    return true;
#else
    return false;
#endif
}

Listing list(const QString &path) {
    Listing out;
    auto fail = [&](Status s, const QString &e) {
        out.status = s;
        out.error = e;
        out.entries.clear();
        return out;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(Status::Corrupt, QStringLiteral("Could not open the file: %1").arg(f.errorString()));

    // The end-of-central-directory record sits in the last 22 + 65535 bytes.
    const qint64 fileSize = f.size();
    const qint64 tailSize = qMin<qint64>(fileSize, 22 + 65535);
    if (!f.seek(fileSize - tailSize))
        return fail(Status::Corrupt, damaged());
    const QByteArray tail = f.read(tailSize);
    qsizetype eocd = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i)
        if (u32(tail, i) == kEndSig && i + 22 + u16(tail, i + 20) <= tail.size()) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return fail(Status::Corrupt, damaged());
    if (eocd >= 20 && u32(tail, eocd - 20) == kZip64LocatorSig)
        return fail(Status::Unsupported, QString());

    const quint16 diskNo = u16(tail, eocd + 4);
    const quint16 cdDisk = u16(tail, eocd + 6);
    const quint16 count = u16(tail, eocd + 10);
    const quint32 cdSize = u32(tail, eocd + 12);
    const quint32 cdOffset = u32(tail, eocd + 16);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu)
        return fail(Status::Unsupported, QString());
    if (diskNo != 0 || cdDisk != 0)
        return fail(Status::Unsupported, QString());
    if (qint64(cdOffset) + cdSize > fileSize || !f.seek(cdOffset))
        return fail(Status::Corrupt, damaged());
    const QByteArray cd = f.read(cdSize);
    if (cd.size() != qsizetype(cdSize))
        return fail(Status::Corrupt, damaged());

    qsizetype at = 0;
    for (int n = 0; n < count; ++n) {
        if (at + 46 > cd.size() || u32(cd, at) != kCentralSig)
            return fail(Status::Corrupt, damaged());
        Entry e;
        e.flags = u16(cd, at + 8);
        e.method = u16(cd, at + 10);
        e.crc = u32(cd, at + 16);
        e.compressedSize = u32(cd, at + 20);
        e.size = u32(cd, at + 24);
        const quint16 nameLen = u16(cd, at + 28);
        const quint16 extraLen = u16(cd, at + 30);
        const quint16 commentLen = u16(cd, at + 32);
        e.localOffset = u32(cd, at + 42);
        if (at + 46 + nameLen + extraLen + commentLen > cd.size())
            return fail(Status::Corrupt, damaged());
        e.name = QString::fromUtf8(cd.constData() + at + 46, nameLen);
        if (e.compressedSize == 0xFFFFFFFFu || e.size == 0xFFFFFFFFu || e.localOffset == 0xFFFFFFFFu)
            return fail(Status::Unsupported, QString());
        out.entries.append(e);
        at += 46 + nameLen + extraLen + commentLen;
    }
    return out;
}

Content read(const QString &path, const Entry &e, qint64 maxBytes) {
    Content out;
    auto fail = [&](Status s, const QString &msg) {
        out.status = s;
        out.error = msg;
        out.data.clear();
        return out;
    };
    if (e.flags & 1)  // Encrypted.
        return fail(Status::Unsupported, QString());
    if (e.method != 0 && e.method != 8)
        return fail(Status::Unsupported, QString());
    if (e.method == 8 && !deflateAvailable())
        return fail(Status::Unsupported, QString());
    if (qint64(e.size) > kMaxMember || qint64(e.compressedSize) > kMaxMember)
        return fail(Status::Unsupported, QString());

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(e.localOffset)))
        return fail(Status::Corrupt, damaged());
    const QByteArray local = f.read(30);
    if (local.size() != 30 || u32(local, 0) != kLocalSig)
        return fail(Status::Corrupt, damaged());
    const qint64 dataStart = qint64(e.localOffset) + 30 + u16(local, 26) + u16(local, 28);
    if (dataStart + qint64(e.compressedSize) > f.size() || !f.seek(dataStart))
        return fail(Status::Corrupt, damaged());

    const bool whole = maxBytes <= 0 || qint64(e.size) <= maxBytes;
    const qint64 want = whole ? qint64(e.size) : maxBytes;

    if (e.method == 0) {
        if (e.compressedSize != e.size)
            return fail(Status::Corrupt, damaged());
        out.data = f.read(want);
        if (out.data.size() != want)
            return fail(Status::Corrupt, damaged());
    } else {
#ifndef FAIRFAX_NO_ZLIB
        z_stream s{};
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK)
            return fail(Status::Corrupt, damaged());
        out.data.resize(want);
        s.next_out = reinterpret_cast<Bytef *>(out.data.data());
        s.avail_out = uInt(want);
        qint64 left = qint64(e.compressedSize);
        int rc = Z_OK;
        QByteArray chunk;
        while (rc == Z_OK && s.avail_out > 0) {
            if (s.avail_in == 0) {
                chunk = left > 0 ? f.read(qMin<qint64>(left, 64 * 1024)) : QByteArray();
                if (chunk.isEmpty()) {
                    rc = Z_BUF_ERROR;
                    break;
                }
                left -= chunk.size();
                s.next_in = reinterpret_cast<Bytef *>(chunk.data());
                s.avail_in = uInt(chunk.size());
            }
            rc = inflate(&s, Z_NO_FLUSH);
        }
        const qint64 produced = qint64(s.total_out);
        inflateEnd(&s);
        // The member must yield its stated size (a prefix, the bytes asked for).
        if (produced != want || (rc != Z_OK && rc != Z_STREAM_END))
            return fail(Status::Corrupt, damaged());
#endif
    }

    if (whole && crc32Of(out.data) != e.crc)
        return fail(Status::Corrupt, QStringLiteral("A file inside this backup is damaged (checksum mismatch)."));
    return out;
}

}  // namespace ZipArchive
