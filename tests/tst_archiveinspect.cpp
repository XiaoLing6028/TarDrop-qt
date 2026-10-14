/// The header scan that feeds the risk assessment: what it flags, and which links escape.

#include "ArchiveBuilder.h"

#include "core/ArchiveReader.h"
#include "core/Security.h"
#include "core/Types.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace tardrop;
namespace t = tardrop::test;

Q_DECLARE_METATYPE(tardrop::RiskLevel)
Q_DECLARE_METATYPE(tardrop::test::Member)

class ArchiveInspectTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_counter = 0;

    /// Writes a fresh fixture and returns its header-scan concerns.
    QList<SecurityConcern> scan(const QList<t::Member> &members)
    {
        const QString path = m_dir.filePath(QStringLiteral("fixture-%1.tar.gz").arg(++m_counter));
        if (!t::writeTarGz(path, members)) {
            qWarning("could not write fixture");
            return {};
        }
        const Result<QList<SecurityConcern>> concerns =
            archives::inspect(path, archives::Format::TarGz);
        if (!concerns) {
            qWarning("inspect failed: %s", qPrintable(concerns.error()));
            return {};
        }
        return *concerns;
    }

    static const SecurityConcern *find(const QList<SecurityConcern> &concerns, const QString &path)
    {
        const auto it = std::ranges::find_if(
            concerns, [&path](const SecurityConcern &concern) { return concern.path == path; });
        return it == concerns.cend() ? nullptr : &*it;
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("tardrop"));
        QVERIFY(m_dir.isValid());
    }

    /// Real archives with 0–4 in-package symlinks land on the same tiers as the pure rule.
    void linkCountBoundaries_data()
    {
        QTest::addColumn<int>("links");
        QTest::addColumn<RiskLevel>("level");
        QTest::newRow("0") << 0 << RiskLevel::Low;
        QTest::newRow("1") << 1 << RiskLevel::Low;
        QTest::newRow("2") << 2 << RiskLevel::Medium;
        QTest::newRow("3") << 3 << RiskLevel::Medium;
        QTest::newRow("4") << 4 << RiskLevel::High;
    }

    void linkCountBoundaries()
    {
        QFETCH(int, links);
        QFETCH(RiskLevel, level);

        QList<t::Member> members = {t::directory(QStringLiteral("app/")),
                                    t::file(QStringLiteral("app/libfoo.so.1"), "elf")};
        for (int i = 0; i < links; ++i) {
            members.append(t::symlink(QStringLiteral("app/alias-%1").arg(i),
                                      QStringLiteral("libfoo.so.1")));
        }
        const QList<SecurityConcern> concerns = scan(members);
        QCOMPARE(concerns.size(), links);
        for (const SecurityConcern &concern : concerns) {
            QCOMPARE(concern.kind, ConcernKind::SymbolicLink);
            QCOMPARE(concern.linkTarget, QStringLiteral("libfoo.so.1"));
            QVERIFY(!concern.escapesPackage);
            QVERIFY(concern.overridable);
        }
        QCOMPARE(security::assessRisk(concerns).level, level);
    }

    void symlinkEscapes_data()
    {
        QTest::addColumn<QString>("linkPath");
        QTest::addColumn<QString>("target");
        QTest::addColumn<bool>("escapes");

        QTest::newRow("sibling file") << QStringLiteral("app/lib/a.so")
                                      << QStringLiteral("a.so.1") << false;
        QTest::newRow("up within package") << QStringLiteral("app/lib/a.so")
                                           << QStringLiteral("../bin/a") << false;
        QTest::newRow("package directory itself") << QStringLiteral("app/self")
                                                  << QStringLiteral(".") << false;
        QTest::newRow("out of the package dir") << QStringLiteral("app/lib/a.so")
                                                << QStringLiteral("../../other") << true;
        QTest::newRow("out of the archive") << QStringLiteral("app/evil")
                                            << QStringLiteral("../../etc/passwd") << true;
        QTest::newRow("absolute") << QStringLiteral("app/evil")
                                  << QStringLiteral("/etc/passwd") << true;
        QTest::newRow("down then out") << QStringLiteral("app/evil")
                                       << QStringLiteral("lib/../../../etc") << true;
    }

    /// In a package with one enclosing directory, leaving that directory counts as escaping.
    void symlinkEscapes()
    {
        QFETCH(QString, linkPath);
        QFETCH(QString, target);
        QFETCH(bool, escapes);

        const QList<SecurityConcern> concerns = scan({
            t::directory(QStringLiteral("app/")),
            t::file(QStringLiteral("app/bin/a"), "elf", 0755),
            t::file(QStringLiteral("app/lib/a.so.1"), "elf"),
            t::symlink(linkPath, target),
        });
        QCOMPARE(concerns.size(), 1);
        QCOMPARE(concerns.first().linkTarget, target);
        QCOMPARE(concerns.first().escapesPackage, escapes);

        const RiskAssessment risk = security::assessRisk(concerns);
        QCOMPARE(risk.level, escapes ? RiskLevel::Medium : RiskLevel::Low);
        QCOMPARE(risk.escapingLinks, escapes ? 1 : 0);
    }

    /// Without one enclosing directory, the archive root itself is the package.
    void flatArchiveUsesArchiveRoot()
    {
        const QList<SecurityConcern> concerns = scan({
            t::file(QStringLiteral("a"), "a"),
            t::file(QStringLiteral("dir/b"), "b"),
            t::symlink(QStringLiteral("dir/to-a"), QStringLiteral("../a")),
            t::symlink(QStringLiteral("out"), QStringLiteral("../x")),
        });
        QCOMPARE(concerns.size(), 2);
        QVERIFY(!find(concerns, QStringLiteral("dir/to-a"))->escapesPackage);
        QVERIFY(find(concerns, QStringLiteral("out"))->escapesPackage);
    }

    /// Hard-link targets name another archive member, so they resolve from the archive root.
    void hardLinkEscapes()
    {
        const QList<SecurityConcern> concerns = scan({
            t::directory(QStringLiteral("app/")),
            t::file(QStringLiteral("app/run"), "run", 0755),
            t::hardLink(QStringLiteral("app/copy"), QStringLiteral("app/run")),
            t::hardLink(QStringLiteral("app/shadow"), QStringLiteral("../etc/shadow")),
        });
        QCOMPARE(concerns.size(), 2);
        const SecurityConcern *inside = find(concerns, QStringLiteral("app/copy"));
        const SecurityConcern *outside = find(concerns, QStringLiteral("app/shadow"));
        QVERIFY(inside && outside);
        QCOMPARE(inside->kind, ConcernKind::HardLink);
        QCOMPARE(inside->linkTarget, QStringLiteral("app/run"));
        QVERIFY(!inside->escapesPackage);
        QVERIFY(outside->escapesPackage);
        QCOMPARE(security::assessRisk(concerns).level, RiskLevel::Medium);
    }

    void installScriptDetection_data()
    {
        QTest::addColumn<QList<t::Member>>("members");
        QTest::addColumn<int>("scripts");

        QTest::newRow("top level") << QList<t::Member>{t::file(QStringLiteral("app/install.sh"),
                                                               "#!/bin/sh\n", 0755)}
                                   << 1;
        QTest::newRow("nested, upper case")
            << QList<t::Member>{t::file(QStringLiteral("app/scripts/INSTALL.SH"), "#!/bin/sh\n")}
            << 1;
        QTest::newRow("two scripts")
            << QList<t::Member>{t::file(QStringLiteral("app/install.sh"), "x"),
                                t::file(QStringLiteral("app/extra/install.sh"), "x")}
            << 2;
        QTest::newRow("similar names are fine")
            << QList<t::Member>{t::file(QStringLiteral("app/install.sh.bak"), "x"),
                                t::file(QStringLiteral("app/reinstall.sh"), "x"),
                                t::file(QStringLiteral("app/install.shx"), "x")}
            << 0;
        QTest::newRow("directory named install.sh")
            << QList<t::Member>{t::directory(QStringLiteral("app/install.sh/")),
                                t::file(QStringLiteral("app/install.sh/readme"), "x")}
            << 0;
    }

    void installScriptDetection()
    {
        QFETCH(QList<t::Member>, members);
        QFETCH(int, scripts);

        const QList<SecurityConcern> concerns = scan(members);
        QCOMPARE(concerns.size(), scripts);
        for (const SecurityConcern &concern : concerns) {
            QCOMPARE(concern.kind, ConcernKind::InstallScript);
            QVERIFY(concern.overridable);
        }
        const RiskAssessment risk = security::assessRisk(concerns);
        QCOMPARE(risk.installScripts, scripts);
        QCOMPARE(risk.level, scripts > 0 ? RiskLevel::High : RiskLevel::Low);
    }

    /// Strict extraction refuses install.sh; consented extraction leaves it out and keeps the rest.
    void installScriptIsOmittedNotWritten()
    {
        const QString archive = m_dir.filePath(QStringLiteral("script.tar.gz"));
        QVERIFY(t::writeTarGz(archive, {
                                           t::file(QStringLiteral("app/run"), "run", 0755),
                                           t::file(QStringLiteral("app/install.sh"), "x", 0755),
                                           t::symlink(QStringLiteral("app/up"),
                                                      QStringLiteral("../../x")),
                                       }));

        const QString strictRoot = m_dir.filePath(QStringLiteral("strict"));
        const Status strict = archives::extract(archive, archives::Format::TarGz, strictRoot);
        QVERIFY(!strict);
        QVERIFY(strict.error().contains(QStringLiteral("install.sh")));

        const QString skipRoot = m_dir.filePath(QStringLiteral("skip"));
        QList<SecurityConcern> skipped;
        const Status consented = archives::extract(archive, archives::Format::TarGz, skipRoot,
                                                   archives::MemberPolicy::SkipUnsafe, &skipped);
        QVERIFY2(consented, qPrintable(consented ? QString() : consented.error()));
        QCOMPARE(skipped.size(), 2);
        QVERIFY(QFileInfo::exists(skipRoot + QStringLiteral("/app/run")));
        QVERIFY(!QFileInfo::exists(skipRoot + QStringLiteral("/app/install.sh")));
        QVERIFY(!QFileInfo(skipRoot + QStringLiteral("/app/up")).isSymLink());
        QVERIFY(!QFileInfo::exists(skipRoot + QStringLiteral("/app/up")));
    }

    /// Traversal stays non-overridable, so it never becomes something the user can approve.
    void traversalStaysNonOverridable()
    {
        const QList<SecurityConcern> concerns = scan({
            t::file(QStringLiteral("app/run"), "run"),
            t::file(QStringLiteral("app/../../evil"), "x"),
        });
        QCOMPARE(concerns.size(), 1);
        QCOMPARE(concerns.first().kind, ConcernKind::UnsafePath);
        QVERIFY(!concerns.first().overridable);
    }
};

QTEST_GUILESS_MAIN(ArchiveInspectTest)
#include "tst_archiveinspect.moc"
