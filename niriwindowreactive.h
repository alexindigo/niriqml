#pragma once

#include "niristateprojection.h"
#include <QtQml/qqmlregistration.h>

class NiriWindowReactive : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWindow)
    Q_PROPERTY(QVariant windowId READ qmlWindowId WRITE setQmlWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(QVariant id READ qmlId NOTIFY idChanged)
    Q_PROPERTY(QVariant title READ qmlTitle NOTIFY titleChanged)
    Q_PROPERTY(QVariant appId READ qmlAppId NOTIFY appIdChanged)
    Q_PROPERTY(QVariant pid READ qmlPid NOTIFY pidChanged)
    Q_PROPERTY(QVariant workspaceId READ qmlWorkspaceId NOTIFY workspaceIdChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY isFocusedChanged)
    Q_PROPERTY(bool isFloating READ isFloating NOTIFY isFloatingChanged)
    Q_PROPERTY(bool isUrgent READ isUrgent NOTIFY isUrgentChanged)
    Q_PROPERTY(NiriWindowLayout layout READ layout NOTIFY layoutChanged)
    Q_PROPERTY(QVariant focusTimestamp READ qmlFocusTimestamp NOTIFY focusTimestampChanged)
    Q_PROPERTY(QVariant window READ qmlWindow NOTIFY windowChanged)

public:
    explicit NiriWindowReactive(QObject *parent = nullptr);
    std::optional<quint64> windowId() const;
    void setWindowId(std::optional<quint64> id);
    void setQmlWindowId(const QVariant &id);
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
    std::optional<NiriWindow> window() const;
    QVariant qmlWindowId() const { return NiriIpc::qmlValue(windowId()); }
    QVariant qmlId() const
    {
        return valid() ? NiriIpc::qmlValue(id()) : QVariant::fromValue(nullptr);
    }
    QVariant qmlTitle() const { return NiriIpc::qmlValue(title()); }
    QVariant qmlAppId() const { return NiriIpc::qmlValue(appId()); }
    QVariant qmlPid() const { return NiriIpc::qmlValue(pid()); }
    QVariant qmlWorkspaceId() const { return NiriIpc::qmlValue(workspaceId()); }
    QVariant qmlFocusTimestamp() const { return NiriIpc::qmlValue(focusTimestamp()); }
    QVariant qmlWindow() const { return NiriIpc::qmlValue(window()); }

signals:
    void windowIdChanged();
    void validChanged();
    void idChanged();
    void titleChanged();
    void appIdChanged();
    void pidChanged();
    void workspaceIdChanged();
    void isFocusedChanged();
    void isFloatingChanged();
    void isUrgentChanged();
    void layoutChanged();
    void focusTimestampChanged();
    void windowChanged();

private:
    using Entry = NiriEventStreamState::Entry<NiriWindow>;
    struct Selection
    {
        std::optional<quint64> id;
        Entry record;
    };
    Entry selected() const;
    static NiriProjectionNotifications::Values values(const Selection &selection);
    void notify();
    std::optional<quint64> m_windowId;
    Selection m_announced;
    quint64 m_revision = 0;
    NiriProjectionNotifications m_notifications;
};
