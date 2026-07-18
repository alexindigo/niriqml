// Shared helpers for live tests against a running niri instance.
//
// Provides socket auto-detection (from $NIRI_SOCKET or /run/user/*/niri.wayland-*.sock)
// and a state-polling helper.

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <unistd.h>

#include "niriconnection.h"

// Resolve NIRI_SOCKET (env or auto-detect), export it, connect, wait for
// connection. Calls QSKIP if no socket is available or connect fails.
inline void setupNiriConnection()
{
    QByteArray sock = qgetenv("NIRI_SOCKET");
    if (sock.isEmpty()) {
        QDir runtimeDir(QStringLiteral("/run/user/%1/").arg(::getuid()));
        auto socks = runtimeDir.entryList({ "niri.wayland-*.sock" }, QDir::System | QDir::Files);
        if (!socks.isEmpty())
            sock = (runtimeDir.absolutePath() + "/" + socks.first()).toUtf8();
    }
    if (sock.isEmpty()) {
        QSKIP("No NIRI_SOCKET and no niri socket found — test skipped");
    }
    qputenv("NIRI_SOCKET", sock);

    NiriConnection *conn = NiriConnection::instance();
    QSignalSpy spy(conn, &NiriConnection::connectedChanged);
    conn->connectToSocket();

    if (!conn->isConnected() && !spy.wait(3000)) {
        QSKIP("Failed to connect to niri socket within 3s");
    }
}

// Poll-based wait: returns true when pred() becomes true within the timeout.
inline bool waitForState(const std::function<bool()> &pred, int timeoutMs = 3000)
{
    const int step = 20;
    int waited = 0;
    while (waited < timeoutMs) {
        if (pred())
            return true;
        QTest::qWait(step);
        waited += step;
    }
    return pred();
}
