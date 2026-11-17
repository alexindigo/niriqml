#include <QTest>
#include <future>
#include <vector>

#include "niriutils.h"
#include "niritypes.h"

namespace {
QJsonObject windowJson()
{
    return {{"id", 42},
            {"title", "Test Window"},
            {"app_id", "com.test.App"},
            {"pid", -12345},
            {"workspace_id", 3},
            {"is_focused", true},
            {"is_floating", false},
            {"is_urgent", false},
            {"layout",
             QJsonObject{{"pos_in_scrolling_layout", QJsonArray{3, 1}},
                         {"tile_size", QJsonArray{1420.0, 914.0}},
                         {"window_size", QJsonArray{1420, 914}},
                         {"tile_pos_in_workspace_view", QJsonValue::Null},
                         {"window_offset_in_tile", QJsonArray{0.0, 0.0}}}},
            {"focus_timestamp", QJsonObject{{"secs", 1000}, {"nanos", 500}}}};
}
QJsonObject workspaceJson()
{
    return {{"id", 5},
            {"idx", 2},
            {"name", QJsonValue::Null},
            {"output", "eDP-1"},
            {"is_urgent", false},
            {"is_active", true},
            {"is_focused", true},
            {"active_window_id", 42}};
}
} // namespace

class TestConverter : public QObject
{
    Q_OBJECT
private slots:
    void testToSnakeCase()
    {
        for (const auto &pair : {qMakePair("appId", "app_id"), qMakePair("isFocused", "is_focused"),
                                 qMakePair("posInScrollingLayout", "pos_in_scrolling_layout"),
                                 qMakePair("tilePosInWorkspaceView", "tile_pos_in_workspace_view"),
                                 qMakePair("activeWindowId", "active_window_id"),
                                 qMakePair("windowOffsetInTile", "window_offset_in_tile"),
                                 qMakePair("id", "id"), qMakePair("pid", "pid"), qMakePair("", "")})
            QCOMPARE(toSnakeCase(QByteArray(pair.first)), QByteArray(pair.second));
    }
    void windowFromJson()
    {
        const auto w = fromNiriJson<NiriWindow>(windowJson());
        QCOMPARE(w.id, 42ull);
        QCOMPARE(w.title, std::optional<QString>("Test Window"));
        QCOMPARE(w.appId, std::optional<QString>("com.test.App"));
        QCOMPARE(w.pid, std::optional<qint32>(-12345));
        QCOMPARE(w.workspaceId, std::optional<quint64>(3));
        QVERIFY(w.isFocused);
        QVERIFY(!w.isFloating);
        QVERIFY(!w.isUrgent);
        QVERIFY((w.layout.posInScrollingLayout == std::array<quint64, 2>{3, 1}));
        QVERIFY((w.layout.tileSize == std::array<double, 2>{1420.0, 914.0}));
        QVERIFY((w.layout.windowSize == std::array<qint32, 2>{1420, 914}));
        QVERIFY(!w.layout.tilePosInWorkspaceView);
        QVERIFY((w.layout.windowOffsetInTile == std::array<double, 2>{0, 0}));
        QVERIFY(w.focusTimestamp);
        QCOMPARE(w.focusTimestamp->secs, 1000ull);
        QCOMPARE(w.focusTimestamp->nanos, 500u);
    }
    void windowNullFields()
    {
        auto json = windowJson();
        for (const auto &field : {"title", "app_id", "pid", "workspace_id", "focus_timestamp"})
            json[field] = QJsonValue::Null;
        const auto w = fromNiriJson<NiriWindow>(json);
        QCOMPARE(w.id, 42ull);
        QVERIFY(!w.title);
        QVERIFY(!w.appId);
        QVERIFY(!w.pid);
        QVERIFY(!w.workspaceId);
        QVERIFY(!w.focusTimestamp);
        json["title"] = "";
        QVERIFY(fromNiriJson<NiriWindow>(json).title.has_value());
    }
    void workspaceFromJson()
    {
        const auto ws = fromNiriJson<NiriWorkspace>(workspaceJson());
        QCOMPARE(ws.id, 5ull);
        QCOMPARE(ws.idx, quint8(2));
        QVERIFY(!ws.name);
        QCOMPARE(ws.output, std::optional<QString>("eDP-1"));
        QVERIFY(ws.isActive);
        QCOMPARE(ws.activeWindowId, std::optional<quint64>(42));
    }
    void windowLayoutFromJson()
    {
        const auto layout = fromNiriJson<NiriWindowLayout>(windowJson()["layout"].toObject());
        QVERIFY((layout.tileSize == std::array<double, 2>{1420.0, 914.0}));
        QVERIFY((layout.windowSize == std::array<qint32, 2>{1420, 914}));
        QVERIFY((layout.windowOffsetInTile == std::array<double, 2>{0.0, 0.0}));
    }
    void missingRequiredRejected()
    {
        QVERIFY(!jsonToGadget({{"id", 1}}, QMetaType::fromType<NiriWorkspace>()).isValid());
        auto json = workspaceJson();
        json.remove("name");
        const auto value = jsonToGadget(json, QMetaType::fromType<NiriWorkspace>());
        QVERIFY(value.isValid());
        QVERIFY(!value.value<NiriWorkspace>().name);
    }
    void extraFieldRetained()
    {
        auto json = windowJson();
        json["unknown_new_field"] = 42;
        json["another_unknown"] = "retained";
        const auto w = fromNiriJson<NiriWindow>(json);
        QCOMPARE(w.id, 42ull);
        QCOMPARE(w.title, std::optional<QString>("Test Window"));
        QCOMPARE(w.extensions.size(), 2);
    }
    void u64IdsPreserved()
    {
        const quint64 largeId = 4294967300ull;
        auto json = windowJson();
        json["id"] = qint64(largeId);
        QCOMPARE(fromNiriJson<NiriWindow>(json).id, largeId);
    }
    void keyboardLayoutsFromJson()
    {
        const auto kl = fromNiriJson<NiriKeyboardLayouts>(
                {{"names", QJsonArray{"English (US)", "Russian (Macintosh)"}}, {"current_idx", 0}});
        QCOMPARE(kl.names, QList<QString>({"English (US)", "Russian (Macintosh)"}));
        QCOMPARE(kl.currentIdx, quint8(0));
    }
    void nestedLayoutRecursion()
    {
        auto json = windowJson();
        json["id"] = 99;
        auto layout = json["layout"].toObject();
        layout["tile_size"] = QJsonArray{800.0, 600.0};
        layout["window_size"] = QJsonArray{800, 600};
        json["layout"] = layout;
        json["focus_timestamp"] = QJsonObject{{"secs", 5000}, {"nanos", 0}};
        const auto w = fromNiriJson<NiriWindow>(json);
        QCOMPARE(w.id, 99ull);
        QVERIFY((w.layout.tileSize == std::array<double, 2>{800, 600}));
        QVERIFY((w.layout.windowSize == std::array<qint32, 2>{800, 600}));
        QVERIFY(w.focusTimestamp);
        QCOMPARE(w.focusTimestamp->secs, 5000ull);
        QCOMPARE(w.focusTimestamp->nanos, 0u);
    }
    void outputWithModesFromJson()
    {
        const auto output =
                fromNiriJson<NiriOutput>({{"name", "eDP-1"},
                                          {"make", "BOE"},
                                          {"model", "NE135A1M-NY1"},
                                          {"modes",
                                           QJsonArray{QJsonObject{{"width", 2880},
                                                                  {"height", 1920},
                                                                  {"refresh_rate", 120000},
                                                                  {"is_preferred", true}},
                                                      QJsonObject{{"width", 2880},
                                                                  {"height", 1920},
                                                                  {"refresh_rate", 60001},
                                                                  {"is_preferred", true}}}},
                                          {"current_mode", 0},
                                          {"is_custom_mode", false},
                                          {"vrr_supported", true},
                                          {"vrr_enabled", false}});
        QCOMPARE(output.name, "eDP-1");
        QCOMPARE(output.make, "BOE");
        QCOMPARE(output.modes.size(), 2);
        QCOMPARE(output.modes[0].width, quint16(2880));
        QCOMPARE(output.modes[0].height, quint16(1920));
        QCOMPARE(output.modes[0].refreshRate, quint32(120000));
        QVERIFY(output.modes[0].isPreferred);
        QCOMPARE(output.modes[1].refreshRate, quint32(60001));
        QCOMPARE(output.currentMode, std::optional<quint64>(0));
        QVERIFY(output.vrrSupported);
        QVERIFY(!output.vrrEnabled);
    }
    void reentrantConversionHasNoSharedCache()
    {
        std::vector<std::future<bool>> jobs;
        for (int i = 0; i < 8; ++i) {
            jobs.push_back(std::async(std::launch::async, [] {
                for (int n = 0; n < 100; ++n) {
                    auto json = windowJson();
                    json["id"] = n;
                    const auto window = jsonToGadget(json, QMetaType::fromType<NiriWindow>());
                    const auto workspace =
                            jsonToGadget(workspaceJson(), QMetaType::fromType<NiriWorkspace>());
                    if (window.value<NiriWindow>().id != quint64(n)
                        || workspace.value<NiriWorkspace>().output != "eDP-1")
                        return false;
                }
                return true;
            }));
        }
        for (auto &job : jobs)
            QVERIFY(job.get());
        QCOMPARE(snakeCaseCacheSizeForTesting(), 0);
    }
};

QTEST_MAIN(TestConverter)
#include "test_converter.moc"
