#include "ipcfixtures.h"
#include "nirimock.h"
#include <QDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QtQuickTest>

class ExampleSetup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(int actionCount READ actionCount NOTIFY actionCountChanged)
    Q_PROPERTY(QString lastActionJson READ lastActionJson NOTIFY actionCountChanged)
    Q_PROPERTY(int queryCount READ queryCount NOTIFY queryCountChanged)
public:
    QStringList warnings() const { return m_warnings; }
    int actionCount() const { return m_actionCount; }
    QString lastActionJson() const { return m_lastActionJson; }
    int queryCount() const { return m_queryCount; }
    Q_INVOKABLE void delayNextReplies(int count)
    {
        NiriMock::Fault fault;
        fault.delayMs = 1000;
        for (int i = 0; i < count; ++i)
            m_mock->enqueueFault(fault);
    }
    Q_INVOKABLE void setQueryResults(const QString &value)
    {
        auto output = serdeRecordFixture("output-null");
        output["name"] = value;
        const QJsonObject outputs{{"Outputs", QJsonObject{{value, output}}}};
        const QJsonObject version{{"Version", value}};
        for (const auto &entry : {qMakePair(QStringLiteral("Outputs"), outputs),
                                  qMakePair(QStringLiteral("Version"), version)}) {
            const auto parsed = NiriWire::parse(QJsonDocument(entry.second).toJson());
            if (!parsed)
                qFatal("Invalid query fixture");
            m_mock->setResponse(entry.first, parsed->root);
        }
    }
    Q_INVOKABLE void removeWorkspace()
    {
        m_mock->sendEvent({{"WorkspacesChanged", QJsonObject{{"workspaces", QJsonArray{}}}}});
    }
    Q_INVOKABLE void removeWindow()
    {
        m_mock->sendEvent({{"WindowsChanged", QJsonObject{{"windows", QJsonArray{}}}}});
    }
    Q_INVOKABLE bool capture(const QString &name)
    {
        const QString directory = qEnvironmentVariable("NIRIQML_EXAMPLE_CAPTURE_DIR");
        if (directory.isEmpty())
            return true;
        if (!QDir().mkpath(directory))
            return false;
        for (QWindow *window : QGuiApplication::topLevelWindows()) {
            auto *quick = qobject_cast<QQuickWindow *>(window);
            if (quick && quick->title() == QStringLiteral("niriqml dashboard"))
                return quick->grabWindow().save(directory + QLatin1Char('/') + name + ".png");
        }
        return false;
    }
public slots:
    void applicationAvailable()
    {
        m_mock = new NiriMock(this);
        auto window = windowFixture();
        window["id"] = 7;
        window["title"] = "Mock title";
        window["app_id"] = "mock.app";
        window["workspace_id"] = 1;
        window["is_focused"] = true;
        auto workspace = workspaceFixture();
        workspace["id"] = 1;
        workspace["name"] = "main";
        workspace["output"] = "mock-output";
        workspace["is_active"] = true;
        workspace["is_focused"] = true;
        workspace["active_window_id"] = 7;
        m_mock->setWindows({window});
        m_mock->setWorkspaces({workspace});
        setQueryResults("mock-26.04");
        if (!m_mock->start())
            qFatal("Cannot start example mock");
        connect(m_mock, &NiriMock::requestLine, this, [this](quint64, const QByteArray &line) {
            const auto parsed = NiriWire::parse(line);
            if (parsed && parsed->root.lookup("Action")) {
                ++m_actionCount;
                m_lastActionJson = QString::fromUtf8(line);
                emit actionCountChanged();
            }
            if (parsed && parsed->root.stringValue()
                && (*parsed->root.stringValue() == "Outputs"
                    || *parsed->root.stringValue() == "Version")) {
                ++m_queryCount;
                emit queryCountChanged();
            }
        });
        qputenv("NIRI_SOCKET", m_mock->socketPath().toUtf8());
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        connect(engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                m_warnings.append(error.toString());
            emit warningsChanged();
        });
        QVariantList snippets;
        const QDir root(QStringLiteral(NIRIQML_EXAMPLE_SOURCE));
        const QRegularExpression fence(QStringLiteral("```qml\\n([\\s\\S]*?)\\n```"));
        for (const QString &name : {QStringLiteral("README.md"), QStringLiteral("API.md"),
                                    QStringLiteral("docs/REQUESTS.md")}) {
            QFile file(root.filePath(name));
            if (!file.open(QIODevice::ReadOnly))
                qFatal("Cannot read documentation");
            const QString text = QString::fromUtf8(file.readAll());
            auto matches = fence.globalMatch(text);
            int index = 0;
            while (matches.hasNext()) {
                const QString source = matches.next().captured(1);
                snippets.append(QVariantMap{{"tag", name + QString::number(++index)},
                                            {"source", source},
                                            {"path", root.filePath(name)}});
            }
        }
        if (snippets.isEmpty())
            qFatal("No executable documentation snippets");
        engine->rootContext()->setContextProperty("documentationSnippets", snippets);
        engine->rootContext()->setContextProperty("examplePeer", this);
    }
signals:
    void warningsChanged();
    void actionCountChanged();
    void queryCountChanged();

private:
    NiriMock *m_mock = nullptr;
    QStringList m_warnings;
    int m_actionCount = 0;
    int m_queryCount = 0;
    QString m_lastActionJson;
};

QUICK_TEST_MAIN_WITH_SETUP(Examples, ExampleSetup)
#include "test_examples.moc"
