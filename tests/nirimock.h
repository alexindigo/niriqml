#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>

class NiriMock : public QObject
{
    Q_OBJECT

public:
    explicit NiriMock(QObject *parent = nullptr);
    ~NiriMock();

    bool start();
    void stop();
    QString socketPath() const;

    void setWindows(const QJsonArray &windows);
    void setWorkspaces(const QJsonArray &workspaces);
    void setNextActionError(const QString &message);
    void sendEvent(const QJsonObject &event);

signals:
    void connected();
    void disconnected();
    void requestReceived(const QJsonObject &request);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void sendLine(QLocalSocket *socket, const QJsonObject &obj);
    void handleEventStreamSubscribe(QLocalSocket *socket);
    void handleRequest(QLocalSocket *socket, const QJsonObject &request);

    QLocalServer m_server;
    QString m_socketPath;
    QLocalSocket *m_eventSocket = nullptr;
    QByteArray m_readBuffer;
    QJsonArray m_windows;
    QJsonArray m_workspaces;
    QString m_nextActionError;
};
