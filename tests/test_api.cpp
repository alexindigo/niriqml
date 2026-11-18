#include "nirimock.h"
#include "ipcfixtures.h"
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

class ApiSetup : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        mock = new NiriMock(this);
        auto window = windowFixture();
        window["id"] = 7;
        window["title"] = "Mock title";
        window["is_focused"] = true;
        mock->setWindows({window});
        if (!mock->start())
            qFatal("Cannot start QML mock");
        qputenv("NIRI_SOCKET", mock->socketPath().toUtf8());
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty("mockPeer", mock);
    }

private:
    NiriMock *mock = nullptr;
};
QUICK_TEST_MAIN_WITH_SETUP(TestApi, ApiSetup)
#include "test_api.moc"
