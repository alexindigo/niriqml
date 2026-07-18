#include <QTest>

#include "niriutils.h"
#include "niritypes.h"

class TestConverter : public QObject
{
    Q_OBJECT

private slots:
    void testToSnakeCase();
    void windowFromJson();
    void windowNullFields();
    void workspaceFromJson();
    void windowLayoutFromJson();
    void missingFieldKeepsDefault();
    void extraFieldIgnored();
    void u64IdsPreserved();
    void keyboardLayoutsFromJson();
    void nestedLayoutRecursion();
    void outputWithModesFromJson();
};

void TestConverter::testToSnakeCase()
{
    QCOMPARE(toSnakeCase(QByteArray("appId")), QByteArray("app_id"));
    QCOMPARE(toSnakeCase(QByteArray("isFocused")), QByteArray("is_focused"));
    QCOMPARE(toSnakeCase(QByteArray("posInScrollingLayout")),
             QByteArray("pos_in_scrolling_layout"));
    QCOMPARE(toSnakeCase(QByteArray("tilePosInWorkspaceView")),
             QByteArray("tile_pos_in_workspace_view"));
    QCOMPARE(toSnakeCase(QByteArray("activeWindowId")), QByteArray("active_window_id"));
    QCOMPARE(toSnakeCase(QByteArray("windowOffsetInTile")), QByteArray("window_offset_in_tile"));
    QCOMPARE(toSnakeCase(QByteArray("id")), QByteArray("id"));
    QCOMPARE(toSnakeCase(QByteArray("pid")), QByteArray("pid"));
    QCOMPARE(toSnakeCase(QByteArray("")), QByteArray(""));
}

void TestConverter::windowFromJson()
{
    QJsonObject json;
    json["id"] = 42;
    json["title"] = QStringLiteral("Test Window");
    json["app_id"] = QStringLiteral("com.test.App");
    json["pid"] = 12345;
    json["workspace_id"] = 3;
    json["is_focused"] = true;
    json["is_floating"] = false;
    json["is_urgent"] = false;

    QJsonArray posInScrolling{3, 1};
    QJsonArray tileSize{1420.0, 914.0};
    QJsonArray windowSize{1420, 914};
    QJsonArray offset{0.0, 0.0};
    json["layout"] = QJsonObject{
            {"pos_in_scrolling_layout", posInScrolling},
            {"tile_size", tileSize},
            {"window_size", windowSize},
            {"tile_pos_in_workspace_view", QJsonValue::Null},
            {"window_offset_in_tile", offset},
    };
    json["focus_timestamp"] = QJsonObject{
            {"secs", qint64(1000)},
            {"nanos", 500},
    };

    NiriWindow w = fromNiriJson<NiriWindow>(json);

    QCOMPARE(w.id, 42ull);
    QCOMPARE(w.title, QStringLiteral("Test Window"));
    QCOMPARE(w.appId, QStringLiteral("com.test.App"));
    QCOMPARE(w.pid, 12345u);
    QCOMPARE(w.workspaceId, 3ull);
    QCOMPARE(w.isFocused, true);
    QCOMPARE(w.isFloating, false);
    QCOMPARE(w.isUrgent, false);

    QList<int> expectedPos{3, 1};
    QCOMPARE(w.layout.posInScrollingLayout, expectedPos);
    QList<double> expectedTile{1420.0, 914.0};
    QCOMPARE(w.layout.tileSize, expectedTile);
    QList<int> expectedWSize{1420, 914};
    QCOMPARE(w.layout.windowSize, expectedWSize);
    QList<double> expectedNullPos{};
    QCOMPARE(w.layout.tilePosInWorkspaceView, expectedNullPos);
    QList<double> expectedOff{0.0, 0.0};
    QCOMPARE(w.layout.windowOffsetInTile, expectedOff);

    QCOMPARE(w.focusTimestamp.secs, 1000ull);
    QCOMPARE(w.focusTimestamp.nanos, 500u);
}

void TestConverter::windowNullFields()
{
    QJsonObject json;
    json["id"] = 1;
    json["title"] = QJsonValue::Null;
    json["app_id"] = QJsonValue::Null;
    json["workspace_id"] = QJsonValue::Null;

    NiriWindow w = fromNiriJson<NiriWindow>(json);

    QCOMPARE(w.id, 1ull);
    QCOMPARE(w.title, QString{});
    QCOMPARE(w.appId, QString{});
    QCOMPARE(w.workspaceId, 0ull);
}

void TestConverter::workspaceFromJson()
{
    QJsonObject json;
    json["id"] = 5;
    json["idx"] = 2;
    json["name"] = QJsonValue::Null;
    json["output"] = QStringLiteral("eDP-1");
    json["is_urgent"] = false;
    json["is_active"] = true;
    json["is_focused"] = true;
    json["active_window_id"] = 42;

    NiriWorkspace ws = fromNiriJson<NiriWorkspace>(json);

    QCOMPARE(ws.id, 5ull);
    QCOMPARE(ws.idx, 2);
    QVERIFY(ws.name.isEmpty());
    QCOMPARE(ws.output, QStringLiteral("eDP-1"));
    QCOMPARE(ws.isActive, true);
    QCOMPARE(ws.activeWindowId, 42ull);
}

void TestConverter::windowLayoutFromJson()
{
    QJsonObject json;
    QJsonArray tileSize{1420.0, 914.0};
    QJsonArray windowSize{1420, 914};
    QJsonArray offset{0.0, 0.0};
    json["tile_size"] = tileSize;
    json["window_size"] = windowSize;
    json["window_offset_in_tile"] = offset;

    NiriWindowLayout layout = fromNiriJson<NiriWindowLayout>(json);

    QList<double> expectedTile{1420.0, 914.0};
    QCOMPARE(layout.tileSize, expectedTile);
    QList<int> expectedWSize{1420, 914};
    QCOMPARE(layout.windowSize, expectedWSize);
    QList<double> expectedOff{0.0, 0.0};
    QCOMPARE(layout.windowOffsetInTile, expectedOff);
}

void TestConverter::missingFieldKeepsDefault()
{
    QJsonObject json;
    json["id"] = 1;

    NiriWorkspace ws = fromNiriJson<NiriWorkspace>(json);

    QCOMPARE(ws.id, 1ull);
    QCOMPARE(ws.isUrgent, false);
    QCOMPARE(ws.isActive, false);
    QCOMPARE(ws.name, QString{});
}

void TestConverter::extraFieldIgnored()
{
    QJsonObject json;
    json["id"] = 7;
    json["title"] = QStringLiteral("Test");
    json["unknown_new_field"] = 42;
    json["another_unknown"] = QStringLiteral("ignored");

    NiriWindow w = fromNiriJson<NiriWindow>(json);

    QCOMPARE(w.id, 7ull);
    QCOMPARE(w.title, QStringLiteral("Test"));
}

void TestConverter::u64IdsPreserved()
{
    quint64 largeId = 4294967300ull;
    QJsonObject json;
    json["id"] = qint64(largeId);

    NiriWindow w = fromNiriJson<NiriWindow>(json);

    QCOMPARE(w.id, largeId);
}

void TestConverter::keyboardLayoutsFromJson()
{
    QJsonArray names{QStringLiteral("English (US)"), QStringLiteral("Russian (Macintosh)")};
    QJsonObject json;
    json["names"] = names;
    json["current_idx"] = 0;

    NiriKeyboardLayouts kl = fromNiriJson<NiriKeyboardLayouts>(json);

    QCOMPARE(kl.names.size(), 2);
    QCOMPARE(kl.names[0], QStringLiteral("English (US)"));
    QCOMPARE(kl.names[1], QStringLiteral("Russian (Macintosh)"));
    QCOMPARE(kl.currentIdx, 0);
}

void TestConverter::nestedLayoutRecursion()
{
    QJsonObject layout;
    QJsonArray tileSize{800.0, 600.0};
    QJsonArray windowSize{800, 600};
    QJsonArray offset{0.0, 0.0};
    layout["tile_size"] = tileSize;
    layout["window_size"] = windowSize;
    layout["window_offset_in_tile"] = offset;

    QJsonObject ts;
    ts["secs"] = qint64(5000);
    ts["nanos"] = 0;

    QJsonObject json;
    json["id"] = 99;
    json["layout"] = layout;
    json["focus_timestamp"] = ts;

    NiriWindow w = fromNiriJson<NiriWindow>(json);

    QCOMPARE(w.id, 99ull);
    QList<double> expectedTile{800.0, 600.0};
    QCOMPARE(w.layout.tileSize, expectedTile);
    QList<int> expectedWSize{800, 600};
    QCOMPARE(w.layout.windowSize, expectedWSize);
    QCOMPARE(w.focusTimestamp.secs, 5000ull);
    QCOMPARE(w.focusTimestamp.nanos, 0u);
}

void TestConverter::outputWithModesFromJson()
{
    QJsonArray modes;
    modes.append(QJsonObject{
            {"width", 2880},
            {"height", 1920},
            {"refresh_rate", 120000},
            {"is_preferred", true},
    });
    modes.append(QJsonObject{
            {"width", 2880},
            {"height", 1920},
            {"refresh_rate", 60001},
            {"is_preferred", true},
    });

    QJsonObject json;
    json["name"] = QStringLiteral("eDP-1");
    json["make"] = QStringLiteral("BOE");
    json["model"] = QStringLiteral("NE135A1M-NY1");
    json["modes"] = modes;
    json["current_mode"] = 0;
    json["is_custom_mode"] = false;
    json["vrr_supported"] = true;
    json["vrr_enabled"] = false;

    NiriOutput output = fromNiriJson<NiriOutput>(json);

    QCOMPARE(output.name, QStringLiteral("eDP-1"));
    QCOMPARE(output.make, QStringLiteral("BOE"));
    QCOMPARE(output.modes.size(), 2);
    QCOMPARE(output.modes[0].width, 2880u);
    QCOMPARE(output.modes[0].height, 1920u);
    QCOMPARE(output.modes[0].refreshRate, 120000u);
    QCOMPARE(output.modes[0].isPreferred, true);
    QCOMPARE(output.modes[1].refreshRate, 60001u);
    QCOMPARE(output.currentMode, 0);
    QCOMPARE(output.vrrSupported, true);
    QCOMPARE(output.vrrEnabled, false);
}

QTEST_MAIN(TestConverter)
#include "test_converter.moc"
