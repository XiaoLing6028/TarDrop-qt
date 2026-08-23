/// TarDrop is a user-local, security-conscious installer for portable archives.
///
/// The GUI stays deliberately small; `tardrop-core` does all security-sensitive work.

#include "core/Types.h"
#include "gui/MainWindow.h"

#include <KAboutData>
#include <KCrash>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("tardrop"));

    // Queued signals carry these across the worker/GUI boundary.
    qRegisterMetaType<tardrop::InstalledApp>();
    qRegisterMetaType<tardrop::InstalledRecord>();
    qRegisterMetaType<tardrop::ReleaseInfo>();
    qRegisterMetaType<QList<tardrop::LauncherCandidate>>("QList<tardrop::LauncherCandidate>");

    KAboutData about(QStringLiteral("tardrop"),
                     i18n("TarDrop"),
                     QStringLiteral(TARDROP_VERSION),
                     i18n("A safe, user-local installer for portable application archives"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 The TarDrop authors"));
    about.setHomepage(QStringLiteral("https://github.com/tardrop/tardrop"));
    about.setDesktopFileName(QStringLiteral("org.tardrop.TarDrop"));
    KAboutData::setApplicationData(about);
    KCrash::initialize();

    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName(QStringLiteral("breeze"));
    }
    application.setWindowIcon(QIcon::fromTheme(QStringLiteral("package-x-generic")));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("archive"),
                                 i18n("Portable archives to install."),
                                 i18n("[archive...]"));
    parser.process(application);
    about.processCommandLine(&parser);

    tardrop::gui::MainWindow window;
    window.show();

    QStringList archives;
    for (const QString &argument : parser.positionalArguments()) {
        archives.append(QFileInfo(argument).absoluteFilePath());
    }
    if (!archives.isEmpty()) {
        window.openArchives(archives);
    }

    return application.exec();
}
