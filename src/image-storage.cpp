#include "image-storage.hpp"

#include <QDateTime>
#include <QDir>
#include <QImageWriter>
#include <QSaveFile>
#include <QUuid>

namespace quickpaste {

StorageResult ImageStorage::save(const QImage &image, const QString &directory)
{
    if (image.isNull())
        return {{}, QStringLiteral("Cannot save an empty image.")};
    if (directory.isEmpty())
        return {{}, QStringLiteral("The image storage directory is empty.")};

    QDir destination(QDir(directory).absolutePath());
    if (!destination.mkpath(QStringLiteral(".")))
        return {{}, QStringLiteral("Could not create the image storage directory: %1").arg(destination.path())};

    const QString filename = QStringLiteral("quick-paste-%1-%2.png")
                                 .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")),
                                      QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString path = destination.absoluteFilePath(filename);
    QSaveFile file(path);
    // Never fall back to a direct, potentially partial write.
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return {{}, QStringLiteral("Could not create the image file: %1").arg(file.errorString())};

    QImageWriter writer(&file, "png");
    if (!writer.write(image)) {
        const QString error = writer.errorString();
        file.cancelWriting();
        return {{}, QStringLiteral("Could not encode the image as PNG: %1").arg(error)};
    }
    if (!file.commit())
        return {{}, QStringLiteral("Could not commit the image file: %1").arg(file.errorString())};
    return {path, {}};
}

} // namespace quickpaste
