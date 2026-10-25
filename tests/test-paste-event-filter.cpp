#include "paste-event-filter.hpp"
#include "paste-target.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMimeData>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtTest>

using namespace quickpaste;

class PasteTest : public QObject {
    Q_OBJECT
private slots:
    void allowlist()
    {
        QMainWindow main;
        auto *root = new QWidget(&main);
        main.setCentralWidget(root);
        auto *preview = new QWidget(root);
        preview->setObjectName("preview");
        auto *sources = new QListWidget(root);
        sources->setObjectName("sources");
        auto *scenes = new QListWidget(root);
        scenes->setObjectName("scenes");
        QLineEdit rename(sources);
        QTextEdit text(root);
        QComboBox combo(root);
        combo.setEditable(true);
        QWidget unknown(root);
        QWidget dialog(&main, Qt::Dialog);
        for (QWidget *widget : {preview, static_cast<QWidget *>(sources), sources->viewport(),
                                static_cast<QWidget *>(scenes)})
            QVERIFY(isPasteTargetAllowed(&main, widget, &main, nullptr, nullptr));
        for (QWidget *widget : {static_cast<QWidget *>(&rename), static_cast<QWidget *>(&text),
                                static_cast<QWidget *>(&combo), &unknown, static_cast<QWidget *>(&main), &dialog})
            QVERIFY(!isPasteTargetAllowed(&main, widget, &main, nullptr, nullptr));
        QVERIFY(!isPasteTargetAllowed(&main, preview, &dialog, nullptr, nullptr));
        QVERIFY(!isPasteTargetAllowed(&main, preview, &main, &dialog, nullptr));
        QVERIFY(!isPasteTargetAllowed(&main, preview, &main, nullptr, &dialog));
    }

    void realShortcutDispatch()
    {
        QMainWindow main;
        auto *sources = new QListWidget(&main);
        sources->setObjectName("sources");
        main.setCentralWidget(sources);
        auto *nativePaste = new QAction(&main);
        nativePaste->setShortcut(QKeySequence::Paste);
        main.addAction(nativePaste);
        int nativeCount = 0;
        connect(nativePaste, &QAction::triggered, [&] { ++nativeCount; });
        bool enabled = true;
        int pasted = 0;
        QImage captured;
        PasteEventFilter filter(&main, {
            [&] { return enabled; }, [] { return ImageLimits{}; },
            [&](QImage image) { ++pasted; captured = std::move(image); }, [](QString) {},
        });
        qApp->installEventFilter(&filter);
        main.show();
        main.activateWindow();
        sources->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&main));
        QTRY_COMPARE(QApplication::focusWidget(), sources);
        const auto binding = QKeySequence(QKeySequence::Paste)[0];
        auto paste = [&] { QTest::keyClick(sources, binding.key(), binding.keyboardModifiers()); };

        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(QColor(12, 34, 56, 78));
        QApplication::clipboard()->setImage(image);
        paste();
        QCOMPARE(pasted, 1);
        QCOMPARE(nativeCount, 0);
        QCOMPARE(captured.pixelColor(0, 0), image.pixelColor(0, 0));
        paste();
        QCOMPARE(pasted, 2);
        QCOMPARE(nativeCount, 0);

        // Native source copy must win over the old system image.
        filter.nativeSourceCopied();
        paste();
        QCOMPARE(nativeCount, 1);
        QCOMPARE(pasted, 2);
        filter.nativeSourceClipboardCleared();
        paste();
        QCOMPARE(pasted, 3);
        filter.nativeSourceCopied();
        QApplication::clipboard()->setImage(image);
        paste();
        QCOMPARE(pasted, 4);
        enabled = false;
        paste();
        QCOMPARE(nativeCount, 2);
        enabled = true;
        QApplication::clipboard()->setText("plain text");
        paste();
        QCOMPARE(nativeCount, 3);
        QCOMPARE(pasted, 4);

        auto *rename = new QLineEdit(sources);
        rename->show();
        rename->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), rename);
        auto *mixed = new QMimeData;
        mixed->setText("rename stays native");
        mixed->setImageData(image);
        QApplication::clipboard()->setMimeData(mixed);
        QTest::keyClick(rename, binding.key(), binding.keyboardModifiers());
        QCOMPARE(rename->text(), QStringLiteral("rename stays native"));
        QCOMPARE(pasted, 4);
        QCOMPARE(nativeCount, 3);
        qApp->removeEventFilter(&filter);
    }

    void snapshotAndRepeat()
    {
        QMainWindow main;
        auto *sources = new QListWidget(&main);
        sources->setObjectName("sources");
        main.setCentralWidget(sources);
        int count = 0;
        QImage captured;
        PasteEventFilter filter(&main, {
            [] { return true; }, [] { return ImageLimits{}; },
            [&](QImage image) { ++count; captured = image; }, [](QString) {},
        });
        qApp->installEventFilter(&filter);
        main.show();
        main.activateWindow();
        sources->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&main));
        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QApplication::clipboard()->setImage(image);
        const auto binding = QKeySequence(QKeySequence::Paste)[0];
        class ChangeClipboardOnPress final : public QObject {
        public:
            bool eventFilter(QObject *, QEvent *event) override
            {
                if (event->type() == QEvent::KeyPress &&
                    static_cast<QKeyEvent *>(event)->matches(QKeySequence::Paste))
                    QApplication::clipboard()->setText("changed after reservation");
                return false;
            }
        } mutation;
        // Application filters run in reverse installation order. Mutate only
        // when actual key delivery begins, after Qt's ShortcutOverride phase.
        qApp->installEventFilter(&mutation);
        QTest::keyClick(sources, binding.key(), binding.keyboardModifiers());
        qApp->removeEventFilter(&mutation);
        QCOMPARE(count, 1);
        QCOMPARE(captured.pixelColor(0, 0), QColor(Qt::red));
        QApplication::clipboard()->setImage(image);
        QKeyEvent repeatPress(QEvent::KeyPress, binding.key(), binding.keyboardModifiers(), {}, true);
        QApplication::sendEvent(sources, &repeatPress);
        QCOMPARE(count, 1);
        qApp->removeEventFilter(&filter);
    }
};

QTEST_MAIN(PasteTest)
#include "test-paste-event-filter.moc"
