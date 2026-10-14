/// The approve/deny flow for a refused package, end to end through the real installer.
///
/// HOME and the XDG directories point into a temporary directory, so `~/Applications`, the
/// launcher, and the installed-app database written here are all throwaway.

#include "ArchiveBuilder.h"

#include "core/Installer.h"
#include "core/Types.h"
#include "gui/InstallController.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tardrop;
namespace t = tardrop::test;

Q_DECLARE_METATYPE(tardrop::RiskLevel)

namespace {

constexpr int kTimeoutMs = 30'000;

/// The summary every test expects for the fixture below.
const QString kExpectedSummary = QStringLiteral(
    "High — install.sh script found (1), 1 symlink escapes the package, 1 symlink inside the "
    "package");

} // namespace

class InstallFlowTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_home;
    QString m_archive;

    QString applications() const { return m_home.filePath(QStringLiteral("Applications")); }

    /// Everything in ~/Applications, including hidden staging leftovers.
    QStringList applicationEntries() const
    {
        return QDir(applications())
            .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("tardrop"));
        qputenv("HOME", QFile::encodeName(m_home.path()));
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("data"))));
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("config"))));
        qputenv("XDG_CACHE_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("cache"))));
        QCOMPARE(QDir::homePath(), m_home.path());

        qRegisterMetaType<InstalledApp>();
        qRegisterMetaType<RiskAssessment>();
        qRegisterMetaType<QList<SecurityConcern>>();

        // A usable package that also ships an installer script, a harmless in-package symlink, and
        // one symlink that reaches out of the package.
        m_archive = m_home.filePath(QStringLiteral("demo.tar.gz"));
        QVERIFY(t::writeTarGz(m_archive, {
            t::directory(QStringLiteral("demo/")),
            t::file(QStringLiteral("demo/start-demo"), "#!/bin/sh\necho demo\n", 0755),
            t::file(QStringLiteral("demo/install.sh"), "#!/bin/sh\nrm -rf ~\n", 0755),
            t::file(QStringLiteral("demo/lib/libdemo.so.1"), "not really elf"),
            t::symlink(QStringLiteral("demo/lib/libdemo.so"), QStringLiteral("libdemo.so.1")),
            t::symlink(QStringLiteral("demo/passwd"), QStringLiteral("../../../etc/passwd")),
        }));
    }

    /// The core answers with a question, carrying the assessment, and publishes nothing.
    void refusedPackageCarriesRiskAssessment()
    {
        const Result<InstallResult> result =
            installer::install(m_archive, ExistingChoice::KeepBoth, QString(), {});
        QVERIFY2(result, qPrintable(result ? QString() : result.error()));
        const auto *refused = std::get_if<NeedsSecurityConfirmation>(&*result);
        QVERIFY(refused);
        QCOMPARE(refused->concerns.size(), 3);
        QCOMPARE(refused->risk.level, RiskLevel::High);
        QCOMPARE(refused->risk.summary, kExpectedSummary);
        QCOMPARE(applicationEntries(), QStringList());
    }

    /// Deny behaves as before: the archive stays rejected, nothing is installed, the queue advances.
    void denyKeepsArchiveRejected()
    {
        gui::InstallController controller;
        QSignalSpy prompt(&controller, &gui::InstallController::securityPrompt);
        QSignalSpy installed(&controller, &gui::InstallController::installed);
        QSignalSpy failed(&controller, &gui::InstallController::failed);
        QStringList log;
        connect(&controller, &gui::InstallController::logLine, this,
                [&log](const QString &line) { log.append(line); });

        controller.enqueue({m_archive});
        QVERIFY(prompt.wait(kTimeoutMs));
        QCOMPARE(prompt.size(), 1);
        QCOMPARE(prompt.first().at(0).toString(), m_archive);
        const auto risk = prompt.first().at(2).value<RiskAssessment>();
        QCOMPARE(risk.level, RiskLevel::High);
        QCOMPARE(risk.summary, kExpectedSummary);
        QCOMPARE(prompt.first().at(1).value<QList<SecurityConcern>>().size(), 3);
        QVERIFY(!controller.busy());

        controller.answerSecurity(false);
        QCoreApplication::processEvents();

        QCOMPARE(installed.size(), 0);
        QCOMPARE(failed.size(), 0);
        QVERIFY(log.contains(
            i18n("Installation rejected: %1 did not pass a security check.", m_archive)));
        QVERIFY(log.contains(i18n("Risk assessment: %1", kExpectedSummary)));
        QCOMPARE(applicationEntries(), QStringList());
        QVERIFY(!controller.busy());
        QCOMPARE(controller.queued(), 0);

        // A second answer after the decision is ignored rather than installing anything.
        controller.answerSecurity(true);
        QVERIFY(!installed.wait(500));
    }

    /// Approve installs the rest of the package, with every refused member left out.
    void approveInstallsWithoutRefusedMembers()
    {
        gui::InstallController controller;
        QSignalSpy prompt(&controller, &gui::InstallController::securityPrompt);
        QSignalSpy installed(&controller, &gui::InstallController::installed);
        QSignalSpy failed(&controller, &gui::InstallController::failed);

        controller.enqueue({m_archive});
        QVERIFY(prompt.wait(kTimeoutMs));
        controller.answerSecurity(true);
        QVERIFY(installed.wait(kTimeoutMs));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(prompt.size(), 1); // consent is not asked for twice

        const auto app = installed.first().at(0).value<InstalledApp>();
        QCOMPARE(QFileInfo(app.directory).absolutePath(), applications());
        QVERIFY(QFileInfo(app.executable).isExecutable());
        QCOMPARE(QFileInfo(app.executable).fileName(), QStringLiteral("start-demo"));
        QVERIFY(QFileInfo::exists(app.directory + QStringLiteral("/lib/libdemo.so.1")));
        QVERIFY(QFileInfo::exists(app.desktopFile));

        // Refused members are absent, not merely made harmless.
        QVERIFY(!QFileInfo::exists(app.directory + QStringLiteral("/install.sh")));
        QVERIFY(!QFileInfo(app.directory + QStringLiteral("/lib/libdemo.so")).isSymLink());
        QVERIFY(!QFileInfo(app.directory + QStringLiteral("/passwd")).isSymLink());
        QVERIFY(!QFileInfo::exists(app.directory + QStringLiteral("/passwd")));

        // Only the published app remains: the private staging directory was cleaned up.
        QCOMPARE(applicationEntries(), QStringList{QFileInfo(app.directory).fileName()});

        QVERIFY(installer::uninstall(app));
        QCOMPARE(applicationEntries(), QStringList());
    }
};

QTEST_GUILESS_MAIN(InstallFlowTest)
#include "tst_installflow.moc"
