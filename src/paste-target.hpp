#pragma once

class QWidget;

namespace quickpaste {

// Deliberately narrow: unknown widgets keep their native paste behavior.
bool isPasteTargetAllowed(QWidget *mainWindow, QWidget *focus, QWidget *activeWindow,
                          QWidget *modalWidget, QWidget *popupWidget);

} // namespace quickpaste
