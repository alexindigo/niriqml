#pragma once

#include <QByteArray>
#include <QList>
#include <QByteArrayView>
#include <functional>

class NiriLineBuffer
{
public:
    // A resource default, not a niri protocol maximum. Tune per deployment;
    // calibration against large real snapshot fixtures remains integration work.
    static constexpr qsizetype DefaultMaxFrameBytes = 64 * 1024 * 1024;
    enum class ErrorCode { None, FrameTooLarge, TruncatedFrame };
    struct Error
    {
        ErrorCode code = ErrorCode::None;
        qsizetype actualBytes = 0;
        qsizetype allowedBytes = 0;
    };
    explicit NiriLineBuffer(qsizetype maxFrameBytes = DefaultMaxFrameBytes);

    // Streaming API: emits one frame at a time, preserving bytes except LF.
    // The callback must not reenter/reset this buffer. No complete-frame queue.
    // Empty/JSON-whitespace-only lines are ignored. On failure the caller must
    // retire the connection; errors remain sticky until clear(). Input is scanned
    // only once and no partial frame ever grows beyond maxFrameBytes().
    bool feed(QByteArrayView data, const std::function<void(const QByteArray &)> &onFrame);
    bool finish(); // EOF: complete frames are already delivered; rejects a partial frame.
    const Error &error() const { return m_error; }
    qsizetype maxFrameBytes() const { return m_maxFrameBytes; }
    quint64 scannedBytesForTesting() const { return m_scannedBytes; }

    // Compatibility API for existing batch readers. Trims lines as before and
    // queues completed frames until takeCompleteLines(). New readers should use
    // the streaming overload and bound their socket read chunks as well.
    void feed(const QByteArray &data);
    QList<QByteArray> takeCompleteLines();
    void clear();
    qsizetype pendingBytes() const { return m_buffer.size(); }

private:
    QByteArray m_buffer;
    QList<QByteArray> m_completeLines;
    qsizetype m_maxFrameBytes;
    quint64 m_scannedBytes = 0;
    Error m_error;
    bool m_finished = false;
};
