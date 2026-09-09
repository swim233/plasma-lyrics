#include "frontend/qmlmodule/backendconfig.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSettings>
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

    void persistsProviderOrderAndAmllSettings()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"),
                           QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.sync();
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(config.providerOrder(), QStringLiteral("netease\namll"));
            config.setProviderOrder(QStringLiteral(" AMLL \nnetease\namll\nunknown"));
            config.setAmllIndexUrl(QStringLiteral("https://example.invalid/index.jsonl"));
            config.setAmllContentBaseUrl(QStringLiteral("https://example.invalid/content/"));
            config.setAmllTimeoutMs(12345);
            config.setAmllIndexRefreshHours(36);
            QVERIFY(config.dirty());
            QVERIFY(config.save());
            QVERIFY(!config.dirty());
        }
        {
            auto restored = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(restored.providerOrder(), QStringLiteral("amll\nnetease"));
            QCOMPARE(restored.amllIndexUrl(),
                     QStringLiteral("https://example.invalid/index.jsonl"));
            QCOMPARE(restored.amllContentBaseUrl(),
                     QStringLiteral("https://example.invalid/content/"));
            QCOMPARE(restored.amllTimeoutMs(), 12345);
            QCOMPARE(restored.amllIndexRefreshHours(), 36);
        }
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
