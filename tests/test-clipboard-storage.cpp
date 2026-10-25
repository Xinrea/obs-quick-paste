#include "../src/clipboard-reader.hpp"
#include "../src/image-storage.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace quickpaste;

namespace {

QImage sampleImage()
{
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setPixelColor(0, 0, QColor(12, 34, 56, 78));
    image.setPixelColor(1, 0, QColor(90, 80, 70, 255));
    return image;
}

QByteArray pngBytes(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return bytes;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Two valid one-pixel GIF frames, with a loop extension.
QByteArray animatedGif()
{
    return QByteArray::fromHex(
        "47494638396101000100800000000000ffffff"
        "21ff0b4e45545343415045322e300301000000"
        "21f90400010000002c0000000001000100000202440100"
        "21f90400010000002c00000000010001000002024401003b");
}

} // namespace

class ClipboardStorageTest : public QObject {
    Q_OBJECT

private slots:
    void ignoresOtherTypes();
    void actualImageTakesPriority();
    void pixmapImage();
    void ownsSnapshot();
    void encodedImageTakesPriority();
    void encodedImageRequiresValidContent();
    void localFileUrls();
    void rejectsMultipleAndRemoteUrls();
    void rejectsGif();
    void rejectsAnimatedPng();
    void limits();
    void transparencyRoundtripAndUniqueNames();
    void storageFailures();
};

void ClipboardStorageTest::ignoresOtherTypes()
{
    const ClipboardResult empty = ClipboardReader::read(nullptr);
    QVERIFY(empty.image.isNull());
    QVERIFY(empty.error.isEmpty());

    QMimeData mime;
    mime.setText(QStringLiteral("/tmp/picture.png"));
    mime.setHtml(QStringLiteral("<img src=\"https://example.invalid/image.png\">"));
    mime.setData(QStringLiteral("application/octet-stream"), pngBytes(sampleImage()));
    const ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(result.error.isEmpty());
}

void ClipboardStorageTest::actualImageTakesPriority()
{
    QMimeData mime;
    const QImage image = sampleImage();
    QImage other(1, 1, QImage::Format_RGB32);
    other.fill(Qt::red);
    mime.setImageData(image);
    mime.setData(QStringLiteral("image/png"), pngBytes(other));
    mime.setUrls({QUrl(QStringLiteral("https://example.invalid/a.png")),
                  QUrl(QStringLiteral("https://example.invalid/b.png"))});
    const ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image, image);
}

void ClipboardStorageTest::pixmapImage()
{
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::green);
    QMimeData mime;
    mime.setImageData(QPixmap::fromImage(image));
    const ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image, image);
}

void ClipboardStorageTest::ownsSnapshot()
{
    // Modify externally owned pixels without triggering QImage's detach logic.
    QRgb pixels[] = {qRgba(1, 2, 3, 4), qRgba(5, 6, 7, 8)};
    const QImage borrowed(reinterpret_cast<uchar *>(pixels), 2, 1, QImage::Format_ARGB32);
    QMimeData mime;
    mime.setImageData(borrowed);
    const ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    pixels[0] = qRgba(255, 0, 0, 255);
    mime.clear();
    QCOMPARE(result.image.pixel(0, 0), qRgba(1, 2, 3, 4));
}

void ClipboardStorageTest::encodedImageTakesPriority()
{
    QMimeData mime;
    const QImage image = sampleImage();
    const QByteArray bytes = pngBytes(image);
    QVERIFY(!bytes.isEmpty());
    mime.setData(QStringLiteral("image/png"), bytes);
    mime.setUrls({QUrl(QStringLiteral("https://example.invalid/image.png"))});
    const ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image, image);
}

void ClipboardStorageTest::encodedImageRequiresValidContent()
{
    QMimeData mime;
    mime.setData(QStringLiteral("image/png"), QByteArray("not an image"));
    ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    QMimeData invalidImage;
    invalidImage.setImageData(QStringLiteral("not image data"));
    result = ClipboardReader::read(&invalidImage);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    // A broken representation must not hide another decodable image format.
    mime.setData(QStringLiteral("image/x-png"), pngBytes(sampleImage()));
    result = ClipboardReader::read(&mime);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image, sampleImage());

    invalidImage.setImageData(QImage{});
    invalidImage.setData(QStringLiteral("image/png"), pngBytes(sampleImage()));
    result = ClipboardReader::read(&invalidImage);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image, sampleImage());
}

void ClipboardStorageTest::localFileUrls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("image without extension"));
    QVERIFY(writeBytes(path, pngBytes(sampleImage())));
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image, sampleImage());

    QVERIFY(writeBytes(path, QByteArray("corrupt")));
    result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    mime.setUrls({QUrl::fromLocalFile(directory.path())});
    result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    mime.setUrls({QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.png")))});
    result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());
}

void ClipboardStorageTest::rejectsMultipleAndRemoteUrls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("image.png"));
    QVERIFY(writeBytes(path, pngBytes(sampleImage())));
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(path)});
    ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    for (const QString &url : {QStringLiteral("https://example.invalid/image.png"),
                               QStringLiteral("file://server/share/image.png")}) {
        mime.setUrls({QUrl(url)});
        result = ClipboardReader::read(&mime);
        QVERIFY(result.image.isNull());
        QVERIFY(!result.error.isEmpty());
    }
}

void ClipboardStorageTest::rejectsGif()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("animation.png"));
    const QByteArray gif = animatedGif();
    QVERIFY(writeBytes(path, gif));

    // If the GIF plugin is present, prove this fixture really is animated.
    if (QImageReader::supportedImageFormats().contains("gif")) {
        QImageReader reader(path);
        QVERIFY(reader.canRead());
        QCOMPARE(reader.imageCount(), 2);
        QVERIFY(!reader.read().isNull());
    }

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    mime.clear();
    mime.setData(QStringLiteral("image/png"), gif);
    result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());

    // A single-frame GIF is deliberately unsupported as well.
    mime.setData(QStringLiteral("image/png"), QByteArray::fromHex(
        "47494638396101000100800000000000ffffff"
        "2c00000000010001000002024401003b"));
    result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());
}

void ClipboardStorageTest::rejectsAnimatedPng()
{
    // A valid 1x1, two-frame RGBA APNG (red, then green).
    const QByteArray bytes = QByteArray::fromHex(
        "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c48900000008616354"
        "4c0000000200000000f38d93700000001a6663544c00000000000000010000000100000000000000"
        "000001000a00005a7f30d00000000d49444154789c63f8cfc0f01f00050001ff89993d1d0000001a"
        "6663544c00000001000000010000000100000000000000000001000a0000c10cda04000000116664"
        "415400000002789c6360f8cff01f00040101ff62e7e99c0000000049454e44ae426082");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("animation.png"));
    QVERIFY(writeBytes(path, bytes));
    QImageReader reader(path);
    QVERIFY(reader.canRead());
    QVERIFY(!reader.read().isNull());

    QMimeData mime;
    mime.setData(QStringLiteral("image/png"), bytes);
    ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());
    mime.clear();
    mime.setUrls({QUrl::fromLocalFile(path)});
    result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());
}

void ClipboardStorageTest::limits()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QImage image = sampleImage();
    const QByteArray bytes = pngBytes(image);
    const QString path = directory.filePath(QStringLiteral("image.png"));
    QVERIFY(writeBytes(path, bytes));

    for (int representation = 0; representation < 3; ++representation) {
        QMimeData mime;
        if (representation == 0)
            mime.setImageData(image);
        else if (representation == 1)
            mime.setData(QStringLiteral("image/png"), bytes);
        else
            mime.setUrls({QUrl::fromLocalFile(path)});

        const ClipboardResult atLimit = ClipboardReader::read(&mime, {3, 6});
        QVERIFY2(atLimit.error.isEmpty(), qPrintable(atLimit.error));
        QCOMPARE(atLimit.image, image);
        for (const ImageLimits &limit : {ImageLimits{2, 6}, ImageLimits{3, 5},
                                         ImageLimits{0, 6}, ImageLimits{3, -1}}) {
            const ClipboardResult result = ClipboardReader::read(&mime, limit);
            QVERIFY(result.image.isNull());
            QVERIFY(!result.error.isEmpty());
        }
    }

    QMimeData mime;
    mime.setImageData(QImage());
    const ClipboardResult result = ClipboardReader::read(&mime);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());
}

void ClipboardStorageTest::transparencyRoundtripAndUniqueNames()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("persistent/nested"));
    const QImage image = sampleImage();
    const StorageResult first = ImageStorage::save(image, destination);
    const StorageResult second = ImageStorage::save(image, destination);
    QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
    QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
    QVERIFY(QFileInfo(first.path).isAbsolute());
    QVERIFY(first.path != second.path);
    QVERIFY(QFileInfo::exists(first.path));
    QVERIFY(QFileInfo::exists(second.path));
    QVERIFY(first.path.endsWith(QStringLiteral(".png")));
    QImageReader reader(first.path);
    QCOMPARE(reader.format(), QByteArray("png"));
    const QImage restored = reader.read();
    QCOMPARE(restored, image);
    QCOMPARE(restored.pixelColor(0, 0).alpha(), 78);
    QCOMPARE(restored.pixelColor(2, 1).alpha(), 0);
    QCOMPARE(QDir(destination).entryList(QDir::Files).size(), 2);
}

void ClipboardStorageTest::storageFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString blocker = directory.filePath(QStringLiteral("not-a-directory"));
    const QByteArray sentinel("keep this existing file");
    QVERIFY(writeBytes(blocker, sentinel));
    for (const QString &destination : {QString(), blocker,
                                       blocker + QStringLiteral("/nested")}) {
        const StorageResult result = ImageStorage::save(sampleImage(), destination);
        QVERIFY(result.path.isEmpty());
        QVERIFY(!result.error.isEmpty());
    }
    const StorageResult empty = ImageStorage::save(QImage(), directory.path());
    QVERIFY(empty.path.isEmpty());
    QVERIFY(!empty.error.isEmpty());
    QFile existing(blocker);
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), sentinel);
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 1);
}

QTEST_MAIN(ClipboardStorageTest)
#include "test-clipboard-storage.moc"
