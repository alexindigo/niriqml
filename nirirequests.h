#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <functional>

class NiriRequests : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(bool ok, const QJsonObject &result)>;

    static NiriRequests *instance();

    void send(const QJsonObject &request, Callback callback);

private:
    explicit NiriRequests(QObject *parent = nullptr);
};
