#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QObject>
#include <QtQml/qqmlregistration.h>

struct NiriTimestamp
{
    Q_GADGET
    QML_VALUE_TYPE(niriTimestamp)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(quint64 secs MEMBER secs)
    Q_PROPERTY(quint32 nanos MEMBER nanos)

public:
    quint64 secs = 0;
    quint32 nanos = 0;

    bool operator==(const NiriTimestamp &o) const { return secs == o.secs && nanos == o.nanos; }
};

struct NiriWindowLayout
{
    Q_GADGET
    QML_VALUE_TYPE(niriWindowLayout)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(QList<int> posInScrollingLayout MEMBER posInScrollingLayout)
    Q_PROPERTY(QList<double> tileSize MEMBER tileSize)
    Q_PROPERTY(QList<int> windowSize MEMBER windowSize)
    Q_PROPERTY(QList<double> tilePosInWorkspaceView MEMBER tilePosInWorkspaceView)
    Q_PROPERTY(QList<double> windowOffsetInTile MEMBER windowOffsetInTile)

public:
    QList<int> posInScrollingLayout;
    QList<double> tileSize;
    QList<int> windowSize;
    QList<double> tilePosInWorkspaceView;
    QList<double> windowOffsetInTile;

    bool operator==(const NiriWindowLayout &o) const {
        return posInScrollingLayout == o.posInScrollingLayout
            && tileSize == o.tileSize
            && windowSize == o.windowSize
            && tilePosInWorkspaceView == o.tilePosInWorkspaceView
            && windowOffsetInTile == o.windowOffsetInTile;
    }
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
    Q_PROPERTY(NiriTimestamp focusTimestamp MEMBER focusTimestamp)

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
    NiriTimestamp focusTimestamp;

    bool operator==(const NiriWindow &o) const {
        return id == o.id && title == o.title && appId == o.appId
            && pid == o.pid && workspaceId == o.workspaceId
            && isFocused == o.isFocused && isFloating == o.isFloating
            && isUrgent == o.isUrgent && layout == o.layout
            && focusTimestamp == o.focusTimestamp;
    }
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
    Q_PROPERTY(bool isUrgent MEMBER isUrgent)
    Q_PROPERTY(bool isActive MEMBER isActive)
    Q_PROPERTY(bool isFocused MEMBER isFocused)
    Q_PROPERTY(quint64 activeWindowId MEMBER activeWindowId)

public:
    quint64 id = 0;
    int idx = 0;
    QString name;
    QString output;
    bool isUrgent = false;
    bool isActive = false;
    bool isFocused = false;
    quint64 activeWindowId = 0;

    bool operator==(const NiriWorkspace &o) const {
        return id == o.id && idx == o.idx && name == o.name
            && output == o.output && isUrgent == o.isUrgent
            && isActive == o.isActive && isFocused == o.isFocused
            && activeWindowId == o.activeWindowId;
    }
};

struct NiriMode
{
    Q_GADGET
    QML_VALUE_TYPE(niriMode)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(quint32 width MEMBER width)
    Q_PROPERTY(quint32 height MEMBER height)
    Q_PROPERTY(quint32 refreshRate MEMBER refreshRate)
    Q_PROPERTY(bool isPreferred MEMBER isPreferred)

public:
    quint32 width = 0;
    quint32 height = 0;
    quint32 refreshRate = 0;
    bool isPreferred = false;

    bool operator==(const NiriMode &o) const {
        return width == o.width && height == o.height
            && refreshRate == o.refreshRate && isPreferred == o.isPreferred;
    }
};

struct NiriLogicalOutput
{
    Q_GADGET
    QML_VALUE_TYPE(niriLogicalOutput)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(int x MEMBER x)
    Q_PROPERTY(int y MEMBER y)
    Q_PROPERTY(quint32 width MEMBER width)
    Q_PROPERTY(quint32 height MEMBER height)
    Q_PROPERTY(double scale MEMBER scale)
    Q_PROPERTY(QString transform MEMBER transform)

public:
    int x = 0;
    int y = 0;
    quint32 width = 0;
    quint32 height = 0;
    double scale = 1.0;
    QString transform;

    bool operator==(const NiriLogicalOutput &o) const {
        return x == o.x && y == o.y && width == o.width
            && height == o.height && scale == o.scale
            && transform == o.transform;
    }
};

struct NiriOutput
{
    Q_GADGET
    QML_VALUE_TYPE(niriOutput)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QString make MEMBER make)
    Q_PROPERTY(QString model MEMBER model)
    Q_PROPERTY(QString serial MEMBER serial)
    Q_PROPERTY(QList<int> physicalSize MEMBER physicalSize)
    Q_PROPERTY(QList<NiriMode> modes MEMBER modes)
    Q_PROPERTY(int currentMode MEMBER currentMode)
    Q_PROPERTY(bool isCustomMode MEMBER isCustomMode)
    Q_PROPERTY(bool vrrSupported MEMBER vrrSupported)
    Q_PROPERTY(bool vrrEnabled MEMBER vrrEnabled)
    Q_PROPERTY(NiriLogicalOutput logical MEMBER logical)

public:
    QString name;
    QString make;
    QString model;
    QString serial;
    QList<int> physicalSize;
    QList<NiriMode> modes;
    int currentMode = -1;
    bool isCustomMode = false;
    bool vrrSupported = false;
    bool vrrEnabled = false;
    NiriLogicalOutput logical;

    bool operator==(const NiriOutput &o) const {
        return name == o.name && physicalSize == o.physicalSize
            && modes == o.modes && currentMode == o.currentMode
            && isCustomMode == o.isCustomMode && vrrSupported == o.vrrSupported
            && vrrEnabled == o.vrrEnabled && logical == o.logical;
    }
};

struct NiriKeyboardLayouts
{
    Q_GADGET
    QML_VALUE_TYPE(niriKeyboardLayouts)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(QStringList names MEMBER names)
    Q_PROPERTY(int currentIdx MEMBER currentIdx)

public:
    QStringList names;
    int currentIdx = 0;

    bool operator==(const NiriKeyboardLayouts &o) const {
        return names == o.names && currentIdx == o.currentIdx;
    }
};
