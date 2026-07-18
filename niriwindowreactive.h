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

    Q_PROPERTY(quint64 windowId READ windowId WRITE setWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY windowChanged)
    Q_PROPERTY(quint64 id READ id NOTIFY windowChanged)
    Q_PROPERTY(QString title READ title NOTIFY windowChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY windowChanged)
    Q_PROPERTY(quint32 pid READ pid NOTIFY windowChanged)
    Q_PROPERTY(quint64 workspaceId READ workspaceId NOTIFY windowChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY windowChanged)
    Q_PROPERTY(bool isFloating READ isFloating NOTIFY windowChanged)
    Q_PROPERTY(bool isUrgent READ isUrgent NOTIFY windowChanged)
    Q_PROPERTY(NiriWindowLayout layout READ layout NOTIFY windowChanged)
    Q_PROPERTY(NiriTimestamp focusTimestamp READ focusTimestamp NOTIFY windowChanged)

public:
    explicit NiriWindowReactive(QObject *parent = nullptr);

    quint64 windowId() const;
    void setWindowId(quint64 id);

    bool valid() const;
    quint64 id() const;
    QString title() const;
    QString appId() const;
    quint32 pid() const;
    quint64 workspaceId() const;
    bool isFocused() const;
    bool isFloating() const;
    bool isUrgent() const;
    NiriWindowLayout layout() const;
    NiriTimestamp focusTimestamp() const;

signals:
    void windowIdChanged();
    void windowChanged();

private slots:
    void onWindowOpenedOrChanged(const NiriWindow &window);
    void onWindowClosed(quint64 id);
    void onWindowsChanged(const QVariantList &windows);
    void onWindowFocusChanged(quint64 id);

private:
    void updateFrom(const NiriWindow &w);
    void clear();

    quint64 m_windowId = 0;
    bool m_valid = false;
    NiriWindow m_data;
};
