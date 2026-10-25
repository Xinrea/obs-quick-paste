#include "plugin-settings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace quickpaste {
namespace {
bool valid(const PluginSettings &s)
{
    return std::isfinite(s.maxCanvasRatio) && s.maxCanvasRatio >= 0.01 &&
           s.maxCanvasRatio <= 1.0 && QDir::isAbsolutePath(s.storageDirectory) &&
           s.limits.maxDimension >= 1 && s.limits.maxDimension <= 32768 &&
           s.limits.maxPixels >= 1 && s.limits.maxPixels <= 268435456;
}
}

PluginSettings PluginSettings::defaults(const QString &configDirectory)
{
    PluginSettings s;
    s.storageDirectory = QDir(configDirectory).absoluteFilePath(QStringLiteral("images"));
    return s;
}

PluginSettings PluginSettings::load(const QString &configDirectory, QString *error)
{
    if (error)
        error->clear();
    auto s = defaults(configDirectory);
    QFile file(QDir(configDirectory).filePath(QStringLiteral("config.json")));
    if (!file.exists())
        return s;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        if (error)
            *error = QStringLiteral("Cannot read plugin settings (maximum 64 KiB).");
        return s;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid plugin settings JSON; using defaults.");
        return s;
    }
    const auto o = doc.object();
    s.enabled = o.value("enabled").toBool(s.enabled);
    s.autoCenter = o.value("autoCenter").toBool(s.autoCenter);
    s.maxCanvasRatio = o.value("maxCanvasRatio").toDouble(s.maxCanvasRatio);
    s.autoSelect = o.value("autoSelect").toBool(s.autoSelect);
    s.bringToFront = o.value("bringToFront").toBool(s.bringToFront);
    s.storageDirectory = o.value("storageDirectory").toString(s.storageDirectory);
    s.limits.maxDimension = o.value("maxDimension").toInt(s.limits.maxDimension);
    s.limits.maxPixels = o.value("maxPixels").toInteger(s.limits.maxPixels);
    if (!valid(s)) {
        if (error)
            *error = QStringLiteral("Plugin settings are outside supported limits; using defaults.");
        return defaults(configDirectory);
    }
    return s;
}

bool PluginSettings::save(const QString &configDirectory, QString *error) const
{
    if (error)
        error->clear();
    if (!valid(*this) || !QDir().mkpath(configDirectory)) {
        if (error)
            *error = QStringLiteral("Invalid settings or cannot create the configuration directory.");
        return false;
    }
    const QJsonObject o{
        {"enabled", enabled}, {"autoCenter", autoCenter}, {"maxCanvasRatio", maxCanvasRatio},
        {"autoSelect", autoSelect}, {"bringToFront", bringToFront},
        {"storageDirectory", storageDirectory}, {"maxDimension", limits.maxDimension},
        {"maxPixels", limits.maxPixels},
        // Never enable GC: collections, backups and undo can still refer to files.
        {"automaticCleanup", false},
    };
    QSaveFile file(QDir(configDirectory).filePath(QStringLiteral("config.json")));
    const auto bytes = QJsonDocument(o).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot save settings: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace quickpaste
