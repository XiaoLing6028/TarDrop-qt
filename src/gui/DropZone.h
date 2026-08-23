#pragma once

/// The large, explicit target for pointer drops, with visual drag feedback.

#include "gui/Widgets.h"

#include <QStringList>
#include <QWidget>

class QLabel;

namespace tardrop::gui {

class DropZone : public QWidget
{
    Q_OBJECT
public:
    explicit DropZone(QWidget *parent = nullptr);

Q_SIGNALS:
    /// Emitted with the local paths of everything the user dropped or picked.
    void archivesChosen(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void browse();
    void setHovering(bool hovering);

    bool m_hovering = false;
    QLabel *m_glyph = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_detail = nullptr;
};

} // namespace tardrop::gui
