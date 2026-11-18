#pragma once

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <sys/un.h>

// Own socket storage separately from build/evidence directories. All paths pass
// the same byte-length boundary, including callers that do not call isValid().
class SocketDirectory
{
public:
    SocketDirectory() : m_directory(checkedTemplate()) { }
    bool isValid() const { return m_directory.isValid(); }
    QString filePath(const QString &name) const
    {
        const auto path = m_directory.filePath(name);
        checkPath(path);
        return path;
    }

private:
    static void checkPath(const QString &path)
    {
        const auto bytes = QFile::encodeName(path).size();
        constexpr auto capacity = sizeof(sockaddr_un{}.sun_path);
        // Qt reserves both the leading abstract-namespace byte and trailing NUL
        // on Linux, even for filesystem sockets (QLocalServerPrivate::listen).
        constexpr auto maximum = capacity - 2;
        if (bytes > qsizetype(maximum))
            qFatal("TMPDIR is too long for Unix fixture socket paths (%lld bytes; maximum %zu): "
                   "%s. Set TMPDIR to a shorter disk-backed directory.",
                   static_cast<long long>(bytes), maximum, qPrintable(path));
    }
    static QString checkedTemplate()
    {
        const auto path = QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("nq.XXXXXX"));
        checkPath(path + QStringLiteral("/socket"));
        return path;
    }
    QTemporaryDir m_directory;
};
