#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

struct NiriWindowLayout
{
    Q_GADGET
    QML_VALUE_TYPE(niriWindowLayout)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(QList<int> tileSize MEMBER tileSize)
    Q_PROPERTY(QList<int> windowSize MEMBER windowSize)

public:
    QList<int> tileSize;
    QList<int> windowSize;

    bool operator==(const NiriWindowLayout &o) const
    {
        return tileSize == o.tileSize && windowSize == o.windowSize;
    }
};

struct NiriSize
{
    Q_GADGET
    QML_VALUE_TYPE(niriSize)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(int fixed MEMBER fixed)
    Q_PROPERTY(double proportion MEMBER proportion)

public:
    int fixed = 0;
    double proportion = 0.0;
};

struct NiriSizeChange
{
    Q_GADGET
    QML_VALUE_TYPE(niriSizeChange)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(int setFixed MEMBER setFixed)
    Q_PROPERTY(double setProportion MEMBER setProportion)

public:
    int setFixed = 0;
    double setProportion = 0.0;
};

struct NiriPos
{
    Q_GADGET
    QML_VALUE_TYPE(niriPos)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(int x MEMBER x)
    Q_PROPERTY(int y MEMBER y)

public:
    int x = 0;
    int y = 0;
};

struct NiriOutput
{
    Q_GADGET
    QML_VALUE_TYPE(niriOutput)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(int width MEMBER width)
    Q_PROPERTY(int height MEMBER height)

public:
    QString name;
    int width = 0;
    int height = 0;
};

struct NiriWindow
{
    Q_GADGET
    QML_VALUE_TYPE(niriWindow)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(quint64 id MEMBER id)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString appId MEMBER appId)
    Q_PROPERTY(quint32 pid MEMBER pid)
    Q_PROPERTY(quint64 workspaceId MEMBER workspaceId)
    Q_PROPERTY(bool isFocused MEMBER isFocused)
    Q_PROPERTY(bool isFloating MEMBER isFloating)
    Q_PROPERTY(bool isUrgent MEMBER isUrgent)
    Q_PROPERTY(NiriWindowLayout layout MEMBER layout)

public:
    quint64 id = 0;
    QString title;
    QString appId;
    quint32 pid = 0;
    quint64 workspaceId = 0;
    bool isFocused = false;
    bool isFloating = false;
    bool isUrgent = false;
    NiriWindowLayout layout;
};

struct NiriWorkspace
{
    Q_GADGET
    QML_VALUE_TYPE(niriWorkspace)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(quint64 id MEMBER id)
    Q_PROPERTY(int idx MEMBER idx)
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QString output MEMBER output)
    Q_PROPERTY(bool isActive MEMBER isActive)
    Q_PROPERTY(bool isFocused MEMBER isFocused)
    Q_PROPERTY(quint64 activeWindowId MEMBER activeWindowId)

public:
    quint64 id = 0;
    int idx = 0;
    QString name;
    QString output;
    bool isActive = false;
    bool isFocused = false;
    quint64 activeWindowId = 0;
};
