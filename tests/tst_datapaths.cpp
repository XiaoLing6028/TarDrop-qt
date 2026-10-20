/// Where launchers, icons, and records are written, natively and from inside Flatpak.
///
/// Inside Flatpak, XDG_DATA_HOME is the app's private `~/.var/app` directory, which no desktop
/// launcher scans, so the core must target the host's data home there instead. The test runs
/// whichever branch the environment selects: the native one in a normal build, and the Flatpak one
/// when the tests run inside the Flatpak build sandbox.

#include "core/Paths.h"

#include <KLocalizedString>
#include <KSandbox>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tardrop;

class DataPathsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_home;

    QString sandboxData() const { return m_home.filePath(QStringLiteral("sandbox-data")); }

    /// Every data directory the core hands out, resolved against the same base.
    static void verifyBase(const QString &base)
    {
        const Result<QString> applications = paths::applicationsDatabaseDir();
        const Result<QString> icons = paths::iconsRootDir();
        const Result<QString> data = paths::dataDir();
        QVERIFY(applications && icons && data);
        QCOMPARE(*applications, paths::join(base, QStringLiteral("applications")));
        QCOMPARE(*icons, paths::join(base, QStringLiteral("icons")));
        QCOMPARE(*data, paths::join(base, QStringLiteral("tardrop")));
        QVERIFY(QDir(*applications).exists());
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("tardrop"));
        qputenv("HOME", QFile::encodeName(m_home.path()));
        qputenv("XDG_DATA_HOME", QFile::encodeName(sandboxData()));
        QCOMPARE(QDir::homePath(), m_home.path());
        qInfo("running the %s branch", KSandbox::isFlatpak() ? "Flatpak" : "native");
    }

    /// A host that sets XDG_DATA_HOME has it forwarded as HOST_XDG_DATA_HOME, which wins in Flatpak.
    void followsHostDataHome()
    {
        const QString host = m_home.filePath(QStringLiteral("host-data"));
        qputenv("HOST_XDG_DATA_HOME", QFile::encodeName(host));
        verifyBase(KSandbox::isFlatpak() ? host : sandboxData());
    }

    /// Without one, Flatpak falls back to the specification default the host itself uses.
    void defaultsToLocalShare()
    {
        qunsetenv("HOST_XDG_DATA_HOME");
        verifyBase(KSandbox::isFlatpak() ? m_home.filePath(QStringLiteral(".local/share"))
                                         : sandboxData());
    }

    /// A relative value is invalid per the specification and must not redirect anything.
    void ignoresRelativeHostDataHome()
    {
        qputenv("HOST_XDG_DATA_HOME", QByteArrayLiteral("relative/data"));
        verifyBase(KSandbox::isFlatpak() ? m_home.filePath(QStringLiteral(".local/share"))
                                         : sandboxData());
    }
};

QTEST_GUILESS_MAIN(DataPathsTest)
#include "tst_datapaths.moc"
