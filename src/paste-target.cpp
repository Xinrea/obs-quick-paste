#include "paste-target.hpp"

#include <QAbstractItemView>
#include <QWidget>

namespace quickpaste {

bool isPasteTargetAllowed(QWidget *mainWindow, QWidget *focus, QWidget *activeWindow,
                          QWidget *modalWidget, QWidget *popupWidget)
{
    if (!mainWindow || !focus || activeWindow != mainWindow || modalWidget || popupWidget ||
        focus->window() != mainWindow)
        return false;

    // OBS's UI object names are the only frontend UI coupling. Never cast to
    // OBS private classes or allow arbitrary descendants (e.g. rename editors).
    QWidget *preview = mainWindow->findChild<QWidget *>(QStringLiteral("preview"));
    if (preview && focus == preview)
        return true;

    for (const char *name : {"sources", "scenes"}) {
        auto *view = mainWindow->findChild<QAbstractItemView *>(QString::fromLatin1(name));
        if (view && (focus == view || focus == view->viewport()))
            return true;
    }
    return false;
}

} // namespace quickpaste
