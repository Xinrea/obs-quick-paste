#include "clipboard-reader.hpp"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaType>
#include <QMimeData>
#include <QPixmap>
#include <QUrl>
#include <QVariant>
#include <QtEndian>

namespace quickpaste {
namespace {

QString sizeError(const QSize &size, const ImageLimits &limits)
{
    if (size.width() <= 0 || size.height() <= 0)
        return QStringLiteral("The image has invalid or unavailable dimensions.");
    if (size.width() > limits.maxDimension || size.height() > limits.maxDimension ||
        qint64(size.width()) * qint64(size.height()) > limits.maxPixels) {
        return QStringLiteral("The image exceeds the configured dimension or pixel limit.");
    }
    return {};
}

ClipboardResult snapshot(const QImage &image, const ImageLimits &limits)
{
    if (const QString error = sizeError(image.size(), limits); !error.isEmpty())
        return {{}, error};

    // QImage can wrap memory owned by another object. Implicit sharing alone is
    // not enough to keep the clipboard snapshot independent of that memory.
    QImage copy = image.copy();
    if (copy.isNull())
        return {{}, QStringLiteral("Could not allocate a copy of the clipboard image.")};
    return {copy, {}};
}

bool hasPngAnimationControl(QIODevice *device)
{
    if (device->read(8) != QByteArray::fromHex("89504e470d0a1a0a"))
        return false;

    // Qt's PNG handler may expose only APNG's first frame. acTL must precede
    // IDAT, so inspect chunk headers without allocating their payloads.
    while (true) {
        const QByteArray header = device->read(8);
        if (header.size() != 8)
            return false;
        const QByteArray type = header.mid(4, 4);
        if (type == "acTL")
            return true;
        if (type == "IDAT" || type == "IEND")
            return false;
        const qint64 length = qFromBigEndian<quint32>(header.constData());
        const qint64 remaining = device->size() - device->pos();
        if (remaining < 4 || length > remaining - 4 ||
            !device->seek(device->pos() + length + 4)) {
            return false;
        }
    }
}

ClipboardResult decode(QIODevice *device, const ImageLimits &limits)
{
    // Both callers provide seekable devices positioned at the beginning.
    const bool animatedPng = hasPngAnimationControl(device);
    if (!device->seek(0))
        return {{}, QStringLiteral("Could not seek within the image data.")};
    if (animatedPng)
        return {{}, QStringLiteral("Animated images and GIF files are not supported.")};

    QImageReader reader(device);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead())
        return {{}, QStringLiteral("The image cannot be decoded: %1").arg(reader.errorString())};

    // Reject GIF even when it contains just one frame. For other formats, do
    // not silently flatten animations or an animation of unknown frame count.
    const int frameCount = reader.imageCount();
    if (reader.format().toLower() == "gif" || frameCount > 1 ||
        (reader.supportsAnimation() && frameCount != 1)) {
        return {{}, QStringLiteral("Animated images and GIF files are not supported.")};
    }

    // Require metadata before allocating decoded pixels. Retain Qt6's own
    // allocation limit as an additional guard; changing it here would change
    // process-global state in the host application.
    if (const QString error = sizeError(reader.size(), limits); !error.isEmpty())
        return {{}, error};

    QImage image = reader.read();
    if (image.isNull())
        return {{}, QStringLiteral("The image cannot be decoded: %1").arg(reader.errorString())};
    if (const QString error = sizeError(image.size(), limits); !error.isEmpty())
        return {{}, error};
    return {image, {}};
}

ClipboardResult readFile(const QUrl &url, const ImageLimits &limits)
{
    const QString path = url.toLocalFile();
    if (!url.isValid() || !url.isLocalFile() ||
        (!url.host().isEmpty() && url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0) ||
        path.isEmpty() || path.startsWith(QStringLiteral("//")) ||
        path.startsWith(QStringLiteral("\\\\"))) {
        return {{}, QStringLiteral("Only a local image file URL is supported.")};
    }
    if (!QFileInfo(path).isFile())
        return {{}, QStringLiteral("The image URL does not refer to a regular file.")};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, QStringLiteral("Could not open the image file: %1").arg(file.errorString())};
    return decode(&file, limits);
}

} // namespace

ClipboardResult ClipboardReader::read(const QMimeData *mime, const ImageLimits &limits)
{
    if (!mime)
        return {};
    if (limits.maxDimension <= 0 || limits.maxPixels <= 0)
        return {{}, QStringLiteral("Image limits must be positive.")};

    QString imageError;
    if (mime->hasImage()) {
        const QVariant value = mime->imageData();
        if (value.metaType().id() == QMetaType::QImage) {
            const QImage image = qvariant_cast<QImage>(value);
            if (!image.isNull())
                return snapshot(image, limits);
        }
        if (value.metaType().id() == QMetaType::QPixmap) {
            const QPixmap pixmap = qvariant_cast<QPixmap>(value);
            if (!pixmap.isNull()) {
                if (const QString error = sizeError(pixmap.size(), limits); !error.isEmpty())
                    return {{}, error};
                return snapshot(pixmap.toImage(), limits);
            }
        }
        imageError = QStringLiteral("The clipboard image data is invalid.");
    }

    for (const QString &format : mime->formats()) {
        if (!format.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive))
            continue;
        QByteArray bytes = mime->data(format);
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        ClipboardResult result = decode(&buffer, limits);
        if (!result.image.isNull())
            return result;
        imageError = result.error;
    }

    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        if (urls.size() != 1)
            return {{}, QStringLiteral("Exactly one local image file URL is required.")};
        return readFile(urls.constFirst(), limits);
    }
    return {{}, imageError};
}

} // namespace quickpaste
