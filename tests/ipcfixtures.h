#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

inline QJsonObject serdeRecordFixture(const QString &name)
{
    QFile file(QFINDTESTDATA("fixtures/serde-v26.04/vectors.json"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open serde fixture");
    const auto rows = QJsonDocument::fromJson(file.readAll()).object()["vectors"].toArray();
    for (const auto &row : rows) {
        const auto record = row.toObject();
        if (record["name"].toString() == name)
            return record["value"].toObject();
    }
    qFatal("Missing serde fixture");
}

inline QJsonObject windowFixture()
{
    auto record = serdeRecordFixture("window-null");
    record["id"] = 0;
    record["is_floating"] = false;
    return record;
}

inline QJsonObject workspaceFixture()
{
    auto record = serdeRecordFixture("workspace-null");
    record["id"] = 0;
    record["idx"] = 1;
    record["is_urgent"] = false;
    return record;
}
