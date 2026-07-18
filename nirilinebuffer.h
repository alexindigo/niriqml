#pragma once

#include <QByteArray>
#include <QList>

class NiriLineBuffer
{
public:
    void feed(const QByteArray &data);
    QList<QByteArray> takeCompleteLines();
    void clear();
    int pendingBytes() const { return m_buffer.size(); }

private:
    QByteArray m_buffer;
};
