#pragma once

#include <QImage>
#include <QString>

class QMimeData;

namespace quickpaste {

struct ImageLimits {
    int maxDimension = 16384;
    qint64 maxPixels = 64000000;
};

struct ClipboardResult {
    QImage image;
    QString error;
};

class ClipboardReader {
public:
    // An empty image and error mean that no supported clipboard type was present.
    // A successful result owns its pixels, independently of the clipboard.
    static ClipboardResult read(const QMimeData *mime, const ImageLimits &limits = {});
};

} // namespace quickpaste
