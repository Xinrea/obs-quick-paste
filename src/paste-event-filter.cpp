#include "paste-event-filter.hpp"
#include "paste-target.hpp"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QKeySequence>

namespace quickpaste {

PasteEventFilter::PasteEventFilter(QWidget *mainWindow, Callbacks callbacks, QObject *parent)
    : QObject(parent), mainWindow_(mainWindow), callbacks_(std::move(callbacks))
{
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        nativeCopy_ = false;
        errorReported_ = false;
        // A reserved gesture owns its snapshot even if the clipboard changes.
    });
}

void PasteEventFilter::reset()
{
    reservedImage_ = {};
    reservedFocus_.clear();
}

void PasteEventFilter::nativeSourceCopied()
{
    nativeCopy_ = true;
    reset();
}

void PasteEventFilter::nativeSourceClipboardCleared()
{
    nativeCopy_ = false;
    reset();
}

bool PasteEventFilter::allowed() const
{
    return callbacks_.enabled() && !nativeCopy_ &&
           QApplication::applicationState() == Qt::ApplicationActive &&
           isPasteTargetAllowed(mainWindow_, QApplication::focusWidget(),
                                QApplication::activeWindow(), QApplication::activeModalWidget(),
                                QApplication::activePopupWidget());
}

bool PasteEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::FocusOut) {
        reset();
        return false;
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)
        return false;

    auto *key = static_cast<QKeyEvent *>(event);
    if (!key->matches(QKeySequence::Paste))
        return false;

    // Only examine the original delivery to the focus widget, not each
    // propagation through its ancestors.
    if (watched != QApplication::focusWidget())
        return false;

    if (event->type() == QEvent::ShortcutOverride) {
        reset();
        if (!allowed())
            return false;
        const auto result = ClipboardReader::read(QApplication::clipboard()->mimeData(),
                                                 callbacks_.limits());
        if (result.image.isNull()) {
            if (!result.error.isEmpty() && !errorReported_ && callbacks_.error) {
                errorReported_ = true;
                callbacks_.error(result.error);
            }
            return false;
        }
        reservedFocus_ = QApplication::focusWidget();
        reservedImage_ = result.image;
        key->accept();
        return true; // prevent QAction/QShortcut from claiming this gesture
    }

    if (reservedImage_.isNull() || reservedFocus_ != QApplication::focusWidget() || !allowed()) {
        reset();
        return false;
    }
    QImage snapshot = std::move(reservedImage_);
    reset();
    key->accept();
    if (!key->isAutoRepeat())
        callbacks_.paste(std::move(snapshot));
    return true;
}

} // namespace quickpaste
