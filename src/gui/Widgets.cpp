#include "gui/Widgets.h"

#include <KColorScheme>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KService>

#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QUrl>
#include <QVBoxLayout>

namespace tardrop::gui {

QColor accentColor()
{
    return KColorScheme(QPalette::Active, KColorScheme::Window)
        .decoration(KColorScheme::FocusColor)
        .color();
}

QColor positiveColor()
{
    return KColorScheme(QPalette::Active, KColorScheme::Window)
        .foreground(KColorScheme::PositiveText)
        .color();
}

QColor negativeColor()
{
    return KColorScheme(QPalette::Active, KColorScheme::Window)
        .foreground(KColorScheme::NegativeText)
        .color();
}

Card::Card(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setBackgroundRole(QPalette::AlternateBase);
    setAutoFillBackground(true);
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(14, 12, 14, 12);
    m_body->setSpacing(8);
}

QLabel *heading(const QString &text, int pointSizeDelta)
{
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + pointSizeDelta);
    label->setFont(font);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

QLabel *caption(const QString &text, bool wrap)
{
    auto *label = new QLabel(text);
    label->setWordWrap(wrap);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    QFont font = label->font();
    font.setPointSize(std::max(1, font.pointSize() - 1));
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(palette);
    return label;
}

QWidget *emptyState(const QString &title, const QString &detail)
{
    auto *card = new Card;
    auto *label = new QLabel(title);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    card->body()->addWidget(label);
    card->body()->addWidget(caption(detail));
    return card;
}

QIcon applicationIcon(const QString &iconPath)
{
    if (!iconPath.isEmpty() && QFileInfo::exists(iconPath)) {
        const QIcon icon(iconPath);
        // A corrupt or unsupported image still yields a non-null QIcon, so check for real content.
        if (!icon.availableSizes().isEmpty()) {
            return icon;
        }
    }
    return QIcon::fromTheme(QStringLiteral("application-x-executable"));
}

void openInFileManager(const QString &path, QWidget *window)
{
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(path), QStringLiteral("inode/directory"));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window));
    job->start();
}

void launchDesktopFile(const QString &desktopFile, QWidget *window)
{
    const KService::Ptr service(new KService(desktopFile));
    auto *job = new KIO::ApplicationLauncherJob(service);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window));
    job->start();
}

} // namespace tardrop::gui
