#pragma once

#include <QImage>
#include <QString>

namespace quickpaste {

struct StorageResult {
    QString path;
    QString error;
};

class ImageStorage {
public:
    static StorageResult save(const QImage &image, const QString &directory);
};

} // namespace quickpaste
