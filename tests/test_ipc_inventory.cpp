#include "niriactions.h"
#include "nirirequests.h"
#include "nirievents.h"
#include "generated/niriipcinventory_generated.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaClassInfo>
#include <QMetaMethod>
#include <QTest>

static_assert(NiriIpcInventory::Request.size() == 16);
static_assert(NiriIpcInventory::Action.size() == 149);
static_assert(NiriIpcInventory::OutputAction.size() == 10);
static_assert(NiriIpcInventory::Response.size() == 14);
static_assert(NiriIpcInventory::Event.size() == 20);

template <std::size_t N>
QSet<QString> names(const std::array<const char *, N> &values)
{
    QSet<QString> result;
    for (const auto *value : values)
        result.insert(QString::fromLatin1(value));
    return result;
}

class TestIpcInventory : public QObject
{
    Q_OBJECT
    QJsonObject defs;
    QSet<QString> schemaTags(const QString &name) const
    {
        QSet<QString> result;
        const auto branches = defs[name].toObject()["oneOf"].toArray();
        for (const auto &item : branches) {
            const auto branch = item.toObject();
            if (branch.contains("const"))
                result.insert(branch["const"].toString());
            else {
                const auto properties = branch["properties"].toObject();
                for (const auto &key : properties.keys())
                    result.insert(key);
            }
        }
        return result;
    }
    static QSet<QString> methods(const QMetaObject &meta)
    {
        QSet<QString> result;
        for (int i = meta.methodOffset(); i < meta.methodCount(); ++i)
            result.insert(QString::fromLatin1(meta.method(i).name()));
        return result;
    }
private slots:
    void initTestCase()
    {
        QFile file(QFINDTESTDATA("../schema/niri-ipc-v26.04.schema.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        defs = doc.object()["$defs"].toObject();
        QVERIFY(!defs.isEmpty());
    }
    void schemaInventory()
    {
        // The released inventory remains a strict subset; explicit extension
        // selections are checked against the second pinned source, not literals.
        const auto released = defs;
        QFile file(QFINDTESTDATA("../schema/niri-ipc-main-689c1e9.schema.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        defs = QJsonDocument::fromJson(file.readAll()).object()["$defs"].toObject();
        QCOMPARE(schemaTags("Request"), names(NiriIpcInventory::Request));
        QCOMPARE(schemaTags("Action"), names(NiriIpcInventory::Action));
        QCOMPARE(schemaTags("OutputAction"), names(NiriIpcInventory::OutputAction));
        QCOMPARE(schemaTags("Response"), names(NiriIpcInventory::Response));
        QCOMPARE(schemaTags("Event"), names(NiriIpcInventory::Event));
        const auto newer = defs;
        defs = released;
        QCOMPARE(schemaTags("Action").size(), 141);
        QCOMPARE(schemaTags("OutputAction").size(), 9);
        QCOMPARE(schemaTags("Event").size(), 19);
        QVERIFY(names(NiriIpcInventory::Action).contains(schemaTags("Action")));
        QVERIFY(names(NiriIpcInventory::OutputAction).contains(schemaTags("OutputAction")));
        QVERIFY(names(NiriIpcInventory::Event).contains(schemaTags("Event")));
        defs = newer;
    }
    void callableAndSignalSurface()
    {
        QCOMPARE(methods(NiriIpcActions::staticMetaObject), names(NiriIpcInventory::ActionMethods));
        QCOMPARE(methods(NiriIpcRequests::staticMetaObject),
                 names(NiriIpcInventory::RequestMethods));
        QCOMPARE(methods(NiriNativeEvents::staticMetaObject),
                 names(NiriIpcInventory::EventSignals));
        QCOMPARE(methods(NiriIpcEvents::staticMetaObject), names(NiriIpcInventory::EventSignals));
        QCOMPARE(NiriAction::staticMetaObject.enumerator(0).keyCount() - 2, 149);
        QCOMPARE(NiriOutputAction::staticMetaObject.enumerator(0).keyCount() - 2, 10);
        QCOMPARE(NiriEvent::staticMetaObject.enumerator(0).keyCount() - 2, 20);
    }
    void typedReplySurface()
    {
        QObject owner;
        QSet<QString> classes;
        for (const auto &tag : names(NiriIpcInventory::Response)) {
            const auto *reply = NiriIpc::createReply(tag, &owner);
            QVERIFY(reply);
            const auto *meta = reply->metaObject();
            const auto name = QString::fromLatin1(meta->className());
            QCOMPARE(name, "Niri" + tag + "Reply");
            classes.insert(name);
            QVERIFY(meta->indexOfProperty("result") >= 0);
            const auto uncreatable = meta->indexOfClassInfo("QML.Creatable");
            QVERIFY(uncreatable >= 0);
            QCOMPARE(QByteArray(meta->classInfo(uncreatable).value()), QByteArray("false"));
        }
        QCOMPARE(classes.size(), 14);
    }
};

QTEST_MAIN(TestIpcInventory)
#include "test_ipc_inventory.moc"
