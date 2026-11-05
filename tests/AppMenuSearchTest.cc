// SPDX-License-Identifier: GPL-2.0-or-later
// Include the implementation to exercise the private scoring helpers unchanged.
#include "../src/AppMenuSearch.cc"
#include <QTest>

// Scoring has no model dependency. Supply the only provider method referenced
// by the implementation, plus its Qt metadata slots, so this test does not
// need a live KWin or D-Bus menu. No model instance is constructed.
namespace OxygenLim
{
AppMenuModel::~AppMenuModel() = default;
QMenu *AppMenuModel::menu() const { return nullptr; }
void AppMenuModel::onMenuUpdated(QMenu *) {}
void AppMenuModel::onActionChanged() {}
void AppMenuModel::processNext() {}
}

class AppMenuSearchTest : public QObject
{
    Q_OBJECT
private slots:
    void preservesScores_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("label");
        QTest::addColumn<int>("expected");
        QTest::newRow("exact") << QStringLiteral("save") << QStringLiteral("File Save As") << 10890;
        QTest::newRow("prefix-tokens") << QStringLiteral("sav as") << QStringLiteral("File Save As") << 2020;
        QTest::newRow("typo") << QStringLiteral("find replce") << QStringLiteral("Edit Find and Replace") << 1730;
        QTest::newRow("phrase") << QStringLiteral("open recent") << QStringLiteral("File Open Recent Documents") << 11590;
        QTest::newRow("no-match") << QStringLiteral("zzzzzz") << QStringLiteral("File Save As") << 0;
    }
    void preservesScores()
    {
        QFETCH(QString, query);
        QFETCH(QString, label);
        QFETCH(int, expected);
        // An unstarted timer disables the deadline for score assertions.
        const QElapsedTimer noDeadline;
        QCOMPARE(OxygenLim::calculateFuzzyScore(query, OxygenLim::tokenizeText(query), label,
                                               OxygenLim::tokenizeText(label), noDeadline), expected);
    }
    void respectsSharedDeadline()
    {
        const QString query = QStringLiteral("save");
        const QString label = QStringLiteral("File Save As");
        const auto queryTokens = OxygenLim::tokenizeText(query);
        const auto labelTokens = OxygenLim::tokenizeText(label);
        const QElapsedTimer noDeadline;
        QVERIFY(OxygenLim::calculateFuzzyScore(query, queryTokens, label, labelTokens, noDeadline) > 0);

        QElapsedTimer timer;
        timer.start();
        QTest::qWait(20);
        // Repeated comparisons must use the original deadline, not restart it.
        QCOMPARE(OxygenLim::calculateFuzzyScore(query, queryTokens, label, labelTokens, timer), 0);
        QCOMPARE(OxygenLim::calculateFuzzyScore(query, queryTokens, label, labelTokens, timer), 0);
    }
    void boundsImpossibleAssignment()
    {
        const QString query = QStringLiteral("orchid quartz velvet zzz zzz zzz");
        const QString label = QStringLiteral("orchid quartz velvet ").repeated(500) + QStringLiteral("zzz zzz");
        const auto queryTokens = OxygenLim::tokenizeText(query);
        const auto labelTokens = OxygenLim::tokenizeText(label);
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(OxygenLim::calculateFuzzyScore(query, queryTokens, label, labelTokens, timer), 0);
        // Generous allowance for instrumented builds and loaded CI machines;
        // the unbounded Release implementation takes several seconds here.
        QVERIFY2(timer.elapsed() < 1000, "Fuzzy scoring did not yield promptly");
    }
};

QTEST_MAIN(AppMenuSearchTest)
#include "AppMenuSearchTest.moc"
