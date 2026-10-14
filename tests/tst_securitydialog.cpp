/// The warning dialog: it shows the risk level and reason, and its two buttons approve or deny.

#include "core/Types.h"
#include "gui/Dialogs.h"

#include <KLocalizedString>
#include <KMessageWidget>

#include <QListWidget>
#include <QPushButton>
#include <QTest>
#include <QTimer>

using namespace tardrop;

Q_DECLARE_METATYPE(tardrop::RiskLevel)
Q_DECLARE_METATYPE(KMessageWidget::MessageType)

namespace {

const QList<SecurityConcern> kConcerns = {
    SecurityConcern{QStringLiteral("app/install.sh"), QStringLiteral("an install script"), true,
                    ConcernKind::InstallScript, QString(), false},
    SecurityConcern{QStringLiteral("app/passwd"), QStringLiteral("a symbolic link"), true,
                    ConcernKind::SymbolicLink, QStringLiteral("../../etc/passwd"), true},
};

RiskAssessment risk(RiskLevel level, const QString &summary)
{
    RiskAssessment result;
    result.level = level;
    result.summary = summary;
    return result;
}

QPushButton *buttonNamed(const QDialog &dialog, const QString &text)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

} // namespace

class SecurityDialogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { KLocalizedString::setApplicationDomain(QByteArrayLiteral("tardrop")); }

    void showsLevelAndReason_data()
    {
        QTest::addColumn<RiskLevel>("level");
        QTest::addColumn<QString>("summary");
        QTest::addColumn<KMessageWidget::MessageType>("type");
        QTest::newRow("low") << RiskLevel::Low
                             << QStringLiteral("Low — 1 symlink inside the package")
                             << KMessageWidget::Information;
        QTest::newRow("medium") << RiskLevel::Medium
                                << QStringLiteral("Medium — 1 symlink escapes the package")
                                << KMessageWidget::Warning;
        QTest::newRow("high")
            << RiskLevel::High
            << QStringLiteral("High — install.sh script found (1), 2 symlinks escape the package")
            << KMessageWidget::Error;
    }

    void showsLevelAndReason()
    {
        QFETCH(RiskLevel, level);
        QFETCH(QString, summary);
        QFETCH(KMessageWidget::MessageType, type);

        gui::SecurityWarningDialog dialog(QStringLiteral("demo.tar.gz"), kConcerns,
                                          risk(level, summary));
        const QList<KMessageWidget *> banners = dialog.findChildren<KMessageWidget *>();
        QCOMPARE(banners.size(), 1);
        QVERIFY(banners.first()->text().contains(summary.toHtmlEscaped()));
        QCOMPARE(banners.first()->messageType(), type);

        // Each refused member is still listed, with an escaping link's target called out.
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), kConcerns.size());
        QVERIFY(list->item(1)->text().contains(QStringLiteral("../../etc/passwd")));
        QVERIFY(list->item(1)->text().contains(QStringLiteral("outside the package")));
    }

    void approve()
    {
        gui::SecurityWarningDialog dialog(QStringLiteral("demo.tar.gz"), kConcerns,
                                          risk(RiskLevel::High, QStringLiteral("High — x")));
        QPushButton *install = buttonNamed(dialog, i18nc("@action:button", "Install Anyway"));
        QVERIFY(install);
        QTimer::singleShot(0, install, &QPushButton::click);
        QCOMPARE(dialog.exec(), int(QDialog::Accepted));
        QVERIFY(dialog.accepted());
    }

    void deny()
    {
        gui::SecurityWarningDialog dialog(QStringLiteral("demo.tar.gz"), kConcerns,
                                          risk(RiskLevel::Low, QStringLiteral("Low — x")));
        QPushButton *keep = buttonNamed(dialog, i18nc("@action:button", "Keep Rejected"));
        QVERIFY(keep);
        QVERIFY(keep->isDefault()); // Return denies, whatever the level
        QTimer::singleShot(0, keep, &QPushButton::click);
        QCOMPARE(dialog.exec(), int(QDialog::Rejected));
        QVERIFY(!dialog.accepted());
    }

    void escapeDenies()
    {
        gui::SecurityWarningDialog dialog(QStringLiteral("demo.tar.gz"), kConcerns,
                                          risk(RiskLevel::Medium, QStringLiteral("Medium — x")));
        QTimer::singleShot(0, &dialog, [&dialog] { QTest::keyClick(&dialog, Qt::Key_Escape); });
        QCOMPARE(dialog.exec(), int(QDialog::Rejected));
        QVERIFY(!dialog.accepted());
    }
};

QTEST_MAIN(SecurityDialogTest)
#include "tst_securitydialog.moc"
