#include "gui/DropZone.h"

#include "core/ArchiveReader.h"

#include <KLocalizedString>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace tardrop::gui {
namespace {

/// Returns the local file paths of a drag, or an empty list when it carries nothing usable.
QStringList localFiles(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls()) {
        return paths;
    }
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    return paths;
}

} // namespace

DropZone::DropZone(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setMinimumHeight(210);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(6);
    layout->addStretch();

    m_glyph = new QLabel;
    m_glyph->setAlignment(Qt::AlignCenter);
    m_glyph->setPixmap(QIcon::fromTheme(QStringLiteral("archive-insert"),
                                        QIcon::fromTheme(QStringLiteral("document-import")))
                           .pixmap(48, 48));
    layout->addWidget(m_glyph);

    m_title = new QLabel;
    m_title->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 6);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_detail = new QLabel;
    m_detail->setAlignment(Qt::AlignCenter);
    m_detail->setWordWrap(true);
    layout->addWidget(m_detail);

    layout->addSpacing(8);
    auto *browseButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                         i18nc("@action:button", "Open Archive…"));
    browseButton->setToolTip(
        i18nc("@info:tooltip",
              "Choose one or more archives from a file dialog instead of dragging them in."));
    connect(browseButton, &QPushButton::clicked, this, &DropZone::browse);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(browseButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);
    layout->addStretch();

    setHovering(false);
}

void DropZone::setHovering(bool hovering)
{
    m_hovering = hovering;
    m_title->setText(hovering ? i18n("Release to add archive") : i18n("Drop an archive here"));
    m_detail->setText(hovering
                          ? i18n("TarDrop will validate it before making any changes.")
                          : i18n("Tar, gzip, xz, bzip2, and ZIP archives are supported."));
    update();
}

void DropZone::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF area = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    const QColor accent = accentColor();

    QColor fill = m_hovering ? accent : palette().color(QPalette::AlternateBase);
    if (m_hovering) {
        fill.setAlpha(40);
    }
    // QPalette::Mid is nearly invisible in dark schemes, so the resting outline is drawn from the
    // text colour at low opacity instead. That reads correctly in both Breeze Light and Dark.
    QColor idleBorder = palette().color(QPalette::WindowText);
    idleBorder.setAlpha(90);
    QPen pen(m_hovering ? accent : idleBorder);
    pen.setWidthF(m_hovering ? 2.0 : 1.0);
    if (!m_hovering) {
        // A dashed outline reads as "put something here" rather than as a static panel.
        pen.setStyle(Qt::DashLine);
    }

    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRoundedRect(area, 8.0, 8.0);
}

void DropZone::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        browse();
    }
    QWidget::mouseReleaseEvent(event);
}

void DropZone::dragEnterEvent(QDragEnterEvent *event)
{
    if (localFiles(event->mimeData()).isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    setHovering(true);
}

void DropZone::dragLeaveEvent(QDragLeaveEvent *event)
{
    setHovering(false);
    QWidget::dragLeaveEvent(event);
}

void DropZone::dropEvent(QDropEvent *event)
{
    const QStringList paths = localFiles(event->mimeData());
    setHovering(false);
    if (paths.isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    Q_EMIT archivesChosen(paths);
}

void DropZone::browse()
{
    // A plain QFileDialog routes through the XDG desktop portal on Plasma Wayland, so the picker
    // is the same one every other application shows.
    const QStringList paths =
        QFileDialog::getOpenFileNames(this,
                                      i18nc("@title:window", "Choose Portable Application Archives"),
                                      QString(),
                                      archives::nameFilter());
    if (!paths.isEmpty()) {
        Q_EMIT archivesChosen(paths);
    }
}

} // namespace tardrop::gui
