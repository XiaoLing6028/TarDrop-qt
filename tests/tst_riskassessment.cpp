/// The risk tiers shown before the user approves or denies a refused package.

#include "core/Security.h"
#include "core/Types.h"

#include <KLocalizedString>

#include <QTest>

using namespace tardrop;

namespace {

SecurityConcern link(ConcernKind kind, bool escapes)
{
    return SecurityConcern{QStringLiteral("app/link"), QStringLiteral("a link"), true, kind,
                           escapes ? QStringLiteral("../../outside") : QStringLiteral("inside"),
                           escapes};
}

SecurityConcern installScript()
{
    return SecurityConcern{QStringLiteral("app/install.sh"), QStringLiteral("an install script"),
                           true, ConcernKind::InstallScript, QString(), false};
}

/// Builds a concern list from counts, so each data row reads as the rule it exercises.
QList<SecurityConcern> concerns(int symlinksInside,
                                int symlinksEscaping,
                                int hardLinksInside,
                                int hardLinksEscaping,
                                int scripts)
{
    QList<SecurityConcern> result;
    for (int i = 0; i < symlinksInside; ++i) {
        result.append(link(ConcernKind::SymbolicLink, false));
    }
    for (int i = 0; i < symlinksEscaping; ++i) {
        result.append(link(ConcernKind::SymbolicLink, true));
    }
    for (int i = 0; i < hardLinksInside; ++i) {
        result.append(link(ConcernKind::HardLink, false));
    }
    for (int i = 0; i < hardLinksEscaping; ++i) {
        result.append(link(ConcernKind::HardLink, true));
    }
    for (int i = 0; i < scripts; ++i) {
        result.append(installScript());
    }
    return result;
}

} // namespace

Q_DECLARE_METATYPE(tardrop::RiskLevel)

class RiskAssessmentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { KLocalizedString::setApplicationDomain(QByteArrayLiteral("tardrop")); }

    void tiers_data()
    {
        QTest::addColumn<int>("symlinksInside");
        QTest::addColumn<int>("symlinksEscaping");
        QTest::addColumn<int>("hardLinksInside");
        QTest::addColumn<int>("hardLinksEscaping");
        QTest::addColumn<int>("scripts");
        QTest::addColumn<RiskLevel>("level");
        QTest::addColumn<QString>("summary");

        // Link-count boundaries, all links pointing inside the package.
        QTest::newRow("0 links") << 0 << 0 << 0 << 0 << 0 << RiskLevel::Low
                                 << QStringLiteral("Low — no links or install scripts");
        QTest::newRow("1 link") << 1 << 0 << 0 << 0 << 0 << RiskLevel::Low
                                << QStringLiteral("Low — 1 symlink inside the package");
        QTest::newRow("2 links") << 2 << 0 << 0 << 0 << 0 << RiskLevel::Medium
                                 << QStringLiteral("Medium — 2 symlinks inside the package");
        QTest::newRow("3 links") << 3 << 0 << 0 << 0 << 0 << RiskLevel::Medium
                                 << QStringLiteral("Medium — 3 symlinks inside the package");
        QTest::newRow("4 links") << 4 << 0 << 0 << 0 << 0 << RiskLevel::High
                                 << QStringLiteral("High — 4 symlinks inside the package");
        QTest::newRow("5 links") << 5 << 0 << 0 << 0 << 0 << RiskLevel::High
                                 << QStringLiteral("High — 5 symlinks inside the package");

        // Hard links count exactly like symlinks, and the two kinds add up.
        QTest::newRow("1 hard link") << 0 << 0 << 1 << 0 << 0 << RiskLevel::Low
                                     << QStringLiteral("Low — 1 hard link inside the package");
        QTest::newRow("3 hard links") << 0 << 0 << 3 << 0 << 0 << RiskLevel::Medium
                                      << QStringLiteral("Medium — 3 hard links inside the package");
        QTest::newRow("1 symlink + 1 hard link")
            << 1 << 0 << 1 << 0 << 0 << RiskLevel::Medium
            << QStringLiteral("Medium — 1 symlink inside the package, 1 hard link inside the "
                              "package");
        QTest::newRow("2 symlinks + 2 hard links")
            << 2 << 0 << 2 << 0 << 0 << RiskLevel::High
            << QStringLiteral("High — 2 symlinks inside the package, 2 hard links inside the "
                              "package");

        // install.sh is High on its own, whatever else the package holds.
        QTest::newRow("install.sh alone") << 0 << 0 << 0 << 0 << 1 << RiskLevel::High
                                          << QStringLiteral("High — install.sh script found (1)");
        QTest::newRow("install.sh + 1 inside link")
            << 1 << 0 << 0 << 0 << 1 << RiskLevel::High
            << QStringLiteral("High — install.sh script found (1), 1 symlink inside the package");
        QTest::newRow("two install.sh") << 0 << 0 << 0 << 0 << 2 << RiskLevel::High
                                        << QStringLiteral("High — install.sh script found (2)");

        // A single escaping link lifts an otherwise Low package to Medium.
        QTest::newRow("1 escaping symlink")
            << 0 << 1 << 0 << 0 << 0 << RiskLevel::Medium
            << QStringLiteral("Medium — 1 symlink escapes the package");
        QTest::newRow("1 escaping hard link")
            << 0 << 0 << 0 << 1 << 0 << RiskLevel::Medium
            << QStringLiteral("Medium — 1 hard link escapes the package");
        QTest::newRow("3 links, 1 escaping")
            << 2 << 1 << 0 << 0 << 0 << RiskLevel::Medium
            << QStringLiteral("Medium — 1 symlink escapes the package, 2 symlinks inside the "
                              "package");
        QTest::newRow("4 escaping symlinks")
            << 0 << 4 << 0 << 0 << 0 << RiskLevel::High
            << QStringLiteral("High — 4 symlinks escape the package");

        // The example from the feature request, verbatim.
        QTest::newRow("install.sh + 2 escaping symlinks")
            << 0 << 2 << 0 << 0 << 1 << RiskLevel::High
            << QStringLiteral("High — install.sh script found (1), 2 symlinks escape the package");
    }

    void tiers()
    {
        QFETCH(int, symlinksInside);
        QFETCH(int, symlinksEscaping);
        QFETCH(int, hardLinksInside);
        QFETCH(int, hardLinksEscaping);
        QFETCH(int, scripts);
        QFETCH(RiskLevel, level);
        QFETCH(QString, summary);

        const RiskAssessment risk = security::assessRisk(concerns(
            symlinksInside, symlinksEscaping, hardLinksInside, hardLinksEscaping, scripts));
        QCOMPARE(risk.level, level);
        QCOMPARE(risk.summary, summary);
        QCOMPARE(risk.symlinks, symlinksInside + symlinksEscaping);
        QCOMPARE(risk.hardLinks, hardLinksInside + hardLinksEscaping);
        QCOMPARE(risk.escapingLinks, symlinksEscaping + hardLinksEscaping);
        QCOMPARE(risk.installScripts, scripts);
    }

    /// Special files are reported but do not move the tier; unsafe paths never reach a prompt.
    void otherConcernsDoNotChangeTheTier()
    {
        const QList<SecurityConcern> list = {
            SecurityConcern{QStringLiteral("app/fifo"), QStringLiteral("a special file"), true,
                            ConcernKind::SpecialFile, QString(), false},
            SecurityConcern{QStringLiteral("../x"), QStringLiteral("an unsafe path"), false,
                            ConcernKind::UnsafePath, QString(), false},
        };
        const RiskAssessment risk = security::assessRisk(list);
        QCOMPARE(risk.level, RiskLevel::Low);
        QCOMPARE(risk.specialFiles, 1);
        QCOMPARE(risk.summary, QStringLiteral("Low — 1 special file"));
    }
};

QTEST_GUILESS_MAIN(RiskAssessmentTest)
#include "tst_riskassessment.moc"
