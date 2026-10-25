#pragma once

#include "clipboard-reader.hpp"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>

namespace quickpaste {

class PasteEventFilter final : public QObject {
public:
    struct Callbacks {
        std::function<bool()> enabled;
        std::function<ImageLimits()> limits;
        std::function<void(QImage)> paste;
        std::function<void(QString)> error;
    };

    PasteEventFilter(QWidget *mainWindow, Callbacks callbacks, QObject *parent = nullptr);
    // OBS keeps a separate source clipboard. A source copy takes precedence
    // over a stale system image until the system clipboard changes again.
    void nativeSourceCopied();
    void nativeSourceClipboardCleared();
    void reset();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool allowed() const;
    QPointer<QWidget> mainWindow_;
    QPointer<QWidget> reservedFocus_;
    Callbacks callbacks_;
    QImage reservedImage_;
    bool nativeCopy_ = false;
    bool errorReported_ = false;
};

} // namespace quickpaste
