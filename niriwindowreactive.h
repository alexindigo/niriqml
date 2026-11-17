#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "niritypes.h"

class NiriWindowReactive : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWindow)

    Q_PROPERTY(QVariant windowId READ qmlWindowId WRITE setQmlWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY windowChanged)
    Q_PROPERTY(QString id READ qmlId NOTIFY windowChanged)
    Q_PROPERTY(QVariant title READ qmlTitle NOTIFY windowChanged)
    Q_PROPERTY(QVariant appId READ qmlAppId NOTIFY windowChanged)
    Q_PROPERTY(QVariant pid READ qmlPid NOTIFY windowChanged)
    Q_PROPERTY(QVariant workspaceId READ qmlWorkspaceId NOTIFY windowChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY windowChanged)
    Q_PROPERTY(bool isFloating READ isFloating NOTIFY windowChanged)
    Q_PROPERTY(bool isUrgent READ isUrgent NOTIFY windowChanged)
    Q_PROPERTY(NiriWindowLayout layout READ layout NOTIFY windowChanged)
    Q_PROPERTY(QVariant focusTimestamp READ qmlFocusTimestamp NOTIFY windowChanged)

public:
    explicit NiriWindowReactive(QObject *parent = nullptr);

    std::optional<quint64> windowId() const;
    void setWindowId(std::optional<quint64> id);

    bool valid() const;
    quint64 id() const;
    std::optional<QString> title() const;
    std::optional<QString> appId() const;
    std::optional<qint32> pid() const;
    std::optional<quint64> workspaceId() const;
    bool isFocused() const;
    bool isFloating() const;
    bool isUrgent() const;
    NiriWindowLayout layout() const;
    std::optional<NiriTimestamp> focusTimestamp() const;

    QVariant qmlWindowId() const { return NiriIpc::qmlValue(windowId()); }
    void setQmlWindowId(const QVariant &id);
    QString qmlId() const { return QString::number(this->id()); }
    QVariant qmlTitle() const { return NiriIpc::qmlValue(title()); }
    QVariant qmlAppId() const { return NiriIpc::qmlValue(appId()); }
    QVariant qmlPid() const { return NiriIpc::qmlValue(pid()); }
    QVariant qmlWorkspaceId() const { return NiriIpc::qmlValue(workspaceId()); }
    QVariant qmlFocusTimestamp() const { return NiriIpc::qmlValue(focusTimestamp()); }

signals:
    void windowIdChanged();
    void windowChanged();

private slots:
    void onWindowOpenedOrChanged(const NiriWindow &window);
    void onWindowClosed(quint64 id);
    void onWindowsChanged(const QVariantList &windows);
    void onWindowFocusChanged(std::optional<quint64> id);

private:
    void updateFrom(const NiriWindow &w);
    void clear();

    std::optional<quint64> m_windowId;
    bool m_valid = false;
    NiriWindow m_data;
};
