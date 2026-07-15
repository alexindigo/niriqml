#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class NiriWindowReactive : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NiriWindow)

    Q_PROPERTY(quint64 windowId READ windowId WRITE setWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(quint64 id READ id NOTIFY windowChanged)
    Q_PROPERTY(QString title READ title NOTIFY windowChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY windowChanged)
    Q_PROPERTY(bool isFocused READ isFocused NOTIFY windowChanged)
    Q_PROPERTY(bool isFloating READ isFloating NOTIFY windowChanged)

public:
    explicit NiriWindowReactive(QObject *parent = nullptr);

    quint64 windowId() const;
    void setWindowId(quint64 id);

    quint64 id() const;
    QString title() const;
    QString appId() const;
    bool isFocused() const;
    bool isFloating() const;

signals:
    void windowIdChanged();
    void windowChanged();

private:
    quint64 m_windowId = 0;
    quint64 m_id = 0;
    QString m_title;
    QString m_appId;
    bool m_isFocused = false;
    bool m_isFloating = false;
};
