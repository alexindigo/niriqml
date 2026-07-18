#include "nirilinebuffer.h"

void NiriLineBuffer::feed(const QByteArray &data)
{
    m_buffer += data;
}

QList<QByteArray> NiriLineBuffer::takeCompleteLines()
{
    QList<QByteArray> lines;
    while (true) {
        int idx = m_buffer.indexOf('\n');
        if (idx < 0)
            break;
        QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer.remove(0, idx + 1);
        if (!line.isEmpty())
            lines.append(line);
    }
    return lines;
}

void NiriLineBuffer::clear()
{
    m_buffer.clear();
}
