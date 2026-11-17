#pragma once

#include "nirieventstreamstate.h"
#include <QObject>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

class NiriState : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(NiriState)
    Q_PROPERTY(QVariant focusedWindowId READ qmlFocusedWindowId NOTIFY focusedWindowIdChanged)
    Q_PROPERTY(
            QVariant focusedWindowAppId READ qmlFocusedWindowAppId NOTIFY focusedWindowAppIdChanged)
    Q_PROPERTY(
            QVariant focusedWindowTitle READ qmlFocusedWindowTitle NOTIFY focusedWindowTitleChanged)
    Q_PROPERTY(
            QVariant focusedWorkspaceId READ qmlFocusedWorkspaceId NOTIFY focusedWorkspaceIdChanged)
    Q_PROPERTY(QVariant activeOutput READ qmlActiveOutput NOTIFY activeOutputChanged)
    Q_PROPERTY(QVariant focusedWindow READ qmlFocusedWindow NOTIFY focusedWindowChanged)
    Q_PROPERTY(QVariant focusedWorkspace READ qmlFocusedWorkspace NOTIFY focusedWorkspaceChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY workspaceCountChanged)
    Q_PROPERTY(int castCount READ castCount NOTIFY castCountChanged)
    Q_PROPERTY(
            bool initialStateReceived READ initialStateReceived NOTIFY initialStateReceivedChanged)
    Q_PROPERTY(QVariantMap windowsById READ windowsById NOTIFY windowsChanged)
    Q_PROPERTY(QVariantMap workspacesById READ workspacesById NOTIFY workspacesChanged)
    Q_PROPERTY(QVariantMap castsByStreamId READ castsByStreamId NOTIFY castsChanged)
    Q_PROPERTY(QVariant keyboardLayouts READ qmlKeyboardLayouts NOTIFY keyboardLayoutsChanged)
    Q_PROPERTY(NiriOverview overview READ overview NOTIFY overviewOpenChanged)
    Q_PROPERTY(bool overviewOpen READ overviewOpen NOTIFY overviewOpenChanged)
    Q_PROPERTY(bool configFailed READ configFailed NOTIFY configFailedChanged)

public:
    static NiriState *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static NiriState *instance();
    std::optional<quint64> focusedWindowId() const;
    std::optional<quint64> focusedWorkspaceId() const;
    std::optional<NiriWindow> focusedWindow() const;
    std::optional<NiriWorkspace> focusedWorkspace() const;
    std::optional<QString> focusedWindowAppId() const;
    std::optional<QString> focusedWindowTitle() const;
    std::optional<QString> activeOutput() const;
    QVariant qmlFocusedWindowId() const { return NiriIpc::qmlValue(focusedWindowId()); }
    QVariant qmlFocusedWorkspaceId() const { return NiriIpc::qmlValue(focusedWorkspaceId()); }
    QVariant qmlFocusedWindow() const { return NiriIpc::qmlValue(focusedWindow()); }
    QVariant qmlFocusedWorkspace() const { return NiriIpc::qmlValue(focusedWorkspace()); }
    QVariant qmlFocusedWindowAppId() const { return NiriIpc::qmlValue(focusedWindowAppId()); }
    QVariant qmlFocusedWindowTitle() const { return NiriIpc::qmlValue(focusedWindowTitle()); }
    QVariant qmlActiveOutput() const { return NiriIpc::qmlValue(activeOutput()); }
    int windowCount() const;
    int workspaceCount() const;
    int castCount() const;
    bool initialStateReceived() const;
    std::optional<NiriKeyboardLayouts> keyboardLayouts() const;
    QVariant qmlKeyboardLayouts() const { return NiriIpc::qmlValue(keyboardLayouts()); }
    NiriOverview overview() const;
    bool overviewOpen() const;
    bool configFailed() const;
    std::optional<NiriWindow> nativeWindow(quint64 id) const;
    std::optional<NiriWorkspace> nativeWorkspace(quint64 id) const;
    std::optional<NiriWorkspace> nativeWorkspaceByName(const QString &name) const;
    std::optional<NiriCast> nativeCast(quint64 streamId) const;
    QList<NiriWindow> nativeWindowsSnapshot() const;
    QList<NiriWorkspace> nativeWorkspacesSnapshot() const;
    QList<NiriCast> nativeCastsSnapshot() const;
    Q_INVOKABLE QVariant window(const QVariant &id) const;
    Q_INVOKABLE QVariant workspace(const QVariant &id) const;
    Q_INVOKABLE QVariant workspaceByName(const QString &name) const;
    Q_INVOKABLE QVariant cast(const QVariant &streamId) const;
    Q_INVOKABLE QVariantList windowsSnapshot() const;
    Q_INVOKABLE QVariantList workspacesSnapshot() const;
    Q_INVOKABLE QVariantList castsSnapshot() const;
    Q_INVOKABLE QVariantMap snapshot() const;
    QVariantMap windowsById() const;
    QVariantMap workspacesById() const;
    QVariantMap castsByStreamId() const;

signals:
    void stateChanged();
    void focusedWindowIdChanged();
    void focusedWorkspaceIdChanged();
    void focusedWindowChanged();
    void focusedWorkspaceChanged();
    void focusedWindowTitleChanged();
    void focusedWindowAppIdChanged();
    void activeOutputChanged();
    void windowCountChanged();
    void workspaceCountChanged();
    void castCountChanged();
    void initialStateReceivedChanged();
    void windowsChanged();
    void workspacesChanged();
    void castsChanged();
    void keyboardLayoutsChanged();
    void overviewOpenChanged();
    void configFailedChanged();

private:
    explicit NiriState(QObject *parent = nullptr);
    void updated(const NiriEventStreamState::Changes &changes);
    const NiriEventStreamState &state() const;
    quint64 m_revision = 0;
};
