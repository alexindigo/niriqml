// Shared helpers for live tests against a running niri instance.
//
// Only the explicit test-owned harness endpoint is accepted. Never discover a desktop.

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <unistd.h>

#include "niriconnection.h"

// Missing harness skips direct binary execution; an invoked fixture must connect.
inline void setupNiriConnection()
{
    QByteArray sock = qgetenv("NIRI_SOCKET");
    if (qEnvironmentVariable("NIRIQML_LIVE_FIXTURE") != "1")
        QSKIP("Use tests/live/run-live.sh for a test-owned compositor fixture");
    QVERIFY2(!sock.isEmpty(), "Owned fixture did not supply NIRI_SOCKET");

    NiriConnection *conn = NiriConnection::instance();
    QSignalSpy spy(conn, &NiriConnection::connectedChanged);
    conn->connectToSocket();

    if (!conn->isConnected() && !spy.wait(3000)) {
        QFAIL("Failed to connect to the owned fixture within 3s");
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
