#include "nirilinebuffer.h"
#include <limits>
#include <utility>

namespace {
bool onlyWhitespace(QByteArrayView bytes)
{
    for (const auto ch : bytes) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            return false;
    }
    return true;
}
} // namespace

NiriLineBuffer::NiriLineBuffer(qsizetype maxFrameBytes)
    : m_maxFrameBytes(qMax<qsizetype>(0, maxFrameBytes))
{
}

bool NiriLineBuffer::feed(QByteArrayView data,
                          const std::function<void(const QByteArray &)> &onFrame)
{
    if (m_finished || m_error.code != ErrorCode::None)
        return false;
    // The scan cursor walks only the newly supplied view, never the accumulated
    // partial frame. Append each scanned segment once, rather than once per byte.
    qsizetype start = 0;
    for (qsizetype scan = 0; scan < data.size(); ++scan) {
        ++m_scannedBytes;
        if (data[scan] == '\n') {
            m_buffer.append(data.data() + start, scan - start);
            if (!onlyWhitespace(m_buffer))
                onFrame(m_buffer);
            m_buffer.clear();
            start = scan + 1;
        } else if (scan - start >= m_maxFrameBytes - m_buffer.size()) {
            // Append only the allowed prefix; never copy the offending byte.
            m_buffer.append(data.data() + start, scan - start);
            m_error = {ErrorCode::FrameTooLarge,
                       m_maxFrameBytes == std::numeric_limits<qsizetype>::max()
                               ? m_maxFrameBytes
                               : m_maxFrameBytes + 1,
                       m_maxFrameBytes};
            return false;
        }
    }
    if (start < data.size())
        m_buffer.append(data.data() + start, data.size() - start);
    return true;
}

void NiriLineBuffer::feed(const QByteArray &data)
{
    feed(QByteArrayView(data), [this](const QByteArray &frame) {
        const auto trimmed = frame.trimmed();
        if (!trimmed.isEmpty())
            m_completeLines.append(trimmed);
    });
}

QList<QByteArray> NiriLineBuffer::takeCompleteLines()
{
    return std::exchange(m_completeLines, {});
}

bool NiriLineBuffer::finish()
{
    m_finished = true;
    if (m_error.code != ErrorCode::None)
        return false;
    if (!onlyWhitespace(m_buffer)) {
        m_error = {ErrorCode::TruncatedFrame, m_buffer.size(), m_maxFrameBytes};
        return false;
    }
    m_buffer.clear();
    return true;
}

void NiriLineBuffer::clear()
{
    m_buffer.clear();
    m_completeLines.clear();
    m_scannedBytes = 0;
    m_error = {};
    m_finished = false;
}
