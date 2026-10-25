#include "plugin-settings.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace quickpaste;

class SettingsTest : public QObject {
    Q_OBJECT
private slots:
    void defaults()
    {
        QTemporaryDir dir;
        QString error;
        const auto s = PluginSettings::load(dir.path(), &error);
        QVERIFY(error.isEmpty());
        QVERIFY(s.enabled && s.autoCenter && s.autoSelect && s.bringToFront);
        QCOMPARE(s.maxCanvasRatio, .8);
        QCOMPARE(s.storageDirectory, dir.path() + "/images");
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        auto s = PluginSettings::defaults(dir.path());
        s.enabled = false;
        s.autoCenter = false;
        s.autoSelect = false;
        s.bringToFront = false;
        s.maxCanvasRatio = .5;
        s.limits.maxPixels = 1234567;
        QString error;
        QVERIFY2(s.save(dir.path(), &error), qPrintable(error));
        const auto loaded = PluginSettings::load(dir.path(), &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!loaded.enabled && !loaded.autoCenter && !loaded.autoSelect && !loaded.bringToFront);
        QCOMPARE(loaded.maxCanvasRatio, .5);
        QCOMPARE(loaded.limits.maxPixels, 1234567);
        QFile file(dir.filePath("config.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["automaticCleanup"].toBool(true),
                 false);
    }

    void invalidSettings()
    {
        QTemporaryDir dir;
        auto s = PluginSettings::defaults(dir.path());
        s.storageDirectory = "relative";
        QString error;
        QVERIFY(!s.save(dir.path(), &error));
        QVERIFY(!error.isEmpty());
        QFile file(dir.filePath("config.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"maxCanvasRatio\": -1}");
        file.close();
        QCOMPARE(PluginSettings::load(dir.path(), &error).maxCanvasRatio, .8);
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("not json");
        file.close();
        QVERIFY(PluginSettings::load(dir.path(), &error).enabled);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "test-plugin-settings.moc"
