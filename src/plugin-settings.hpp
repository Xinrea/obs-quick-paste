#pragma once

#include "clipboard-reader.hpp"
#include <QString>

namespace quickpaste {

struct PluginSettings {
    bool enabled = true;
    bool autoCenter = true;
    double maxCanvasRatio = 0.8;
    bool autoSelect = true;
    bool bringToFront = true;
    QString storageDirectory;
    ImageLimits limits;

    static PluginSettings defaults(const QString &configDirectory);
    static PluginSettings load(const QString &configDirectory, QString *error = nullptr);
    bool save(const QString &configDirectory, QString *error = nullptr) const;
};

} // namespace quickpaste
