#include "frontend/qmlmodule/backendconfig.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace {

BackendConfig shellConfig(const QString &command)
{
    return BackendConfig(QStringLiteral("/bin/sh"),
                         {QStringLiteral("-c"), command});
}

} // namespace

class BackendConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void reportsSuccessfulRestart()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QVERIFY(config.restartInProgress());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartSucceeded);
        QVERIFY(!config.restartInProgress());
        QVERIFY(config.restartError().isEmpty());
        QCOMPARE(finished.first().first().toBool(), true);
    }

    void reportsNonZeroExit()
    {
        auto config = shellConfig(QStringLiteral("exit 23"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(config.restartError().contains(QStringLiteral("23")));
        QCOMPARE(finished.first().first().toBool(), false);
    }

    void failedRestartCanBeRetried()
    {
        auto config = shellConfig(QStringLiteral("exit 9"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
    }

    void reportsStartFailure()
    {
        BackendConfig config(QStringLiteral("/does/not/exist/plasma-lyrics-restart"), {});
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(!config.restartError().isEmpty());
        QVERIFY(!config.restartInProgress());
    }

    void reportsCrash()
    {
        auto config = shellConfig(QStringLiteral("kill -SEGV $$"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(config.restartError().contains(QStringLiteral("crashed")));
    }

    void ignoresConcurrentRestart()
    {
        auto config = shellConfig(QStringLiteral("sleep 0.1; exit 0"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QVERIFY(!config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartSucceeded);
    }

    void destroyingWhileRunningIsSafe()
    {
        QElapsedTimer elapsed;
        elapsed.start();
        auto *config = new BackendConfig(QStringLiteral("/bin/sleep"),
                                         {QStringLiteral("10")});
        QVERIFY(config->restartService());
        delete config;
        QVERIFY(elapsed.elapsed() < 2000);
    }
};

QTEST_GUILESS_MAIN(BackendConfigTest)
#include "tst_backendconfig.moc"
