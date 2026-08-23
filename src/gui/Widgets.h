#pragma once

/// Small presentation helpers shared by the pages.
///
/// Everything here is theme-derived: colours come from the active KDE colour scheme rather than
/// being hard-coded, so TarDrop follows Breeze Light, Breeze Dark, and custom schemes alike.

#include <QColor>
#include <QFrame>
#include <QIcon>
#include <QString>

class QLabel;
class QVBoxLayout;
class QWidget;

namespace tardrop::gui {

/// The scheme's focus/decoration colour, used for emphasis and the active drop target.
QColor accentColor();
QColor positiveColor();
QColor negativeColor();

/// A subtly framed container used to group related content and its actions.
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(QWidget *parent = nullptr);

    /// The vertical layout callers add their content to.
    QVBoxLayout *body() const { return m_body; }

private:
    QVBoxLayout *m_body = nullptr;
};

/// A bold section title at the given point-size increase.
QLabel *heading(const QString &text, int pointSizeDelta = 4);

/// De-emphasised explanatory text. Long values wrap by default rather than stretching a card.
QLabel *caption(const QString &text, bool wrap = true);

/// A calm placeholder shown instead of a bare blank area.
QWidget *emptyState(const QString &title, const QString &detail);

/// Loads an installed application's icon, falling back to a generic theme icon.
QIcon applicationIcon(const QString &iconPath);

/// Opens a directory in the user's file manager through KIO, not an external command.
void openInFileManager(const QString &path, QWidget *window);

/// Starts an installed application through its own desktop entry, so Plasma applies the same
/// launch feedback, scope, and service registration it would from the application menu.
void launchDesktopFile(const QString &desktopFile, QWidget *window);

} // namespace tardrop::gui
