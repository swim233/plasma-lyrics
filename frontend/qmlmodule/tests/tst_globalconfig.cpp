#include "frontend/qmlmodule/globalconfig.h"

#include "core/log/configlog.h"
#include "core/store/lyricstore.h"
#include "frontend/qmlmodule/settingslog.h"

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QStandardPaths>
#include <QTest>

namespace {

// GlobalConfig() always opens LyricStore at its default path (DESIGN.md
// decision 41 -- unlike BackendConfig's tests, there is no per-test
// QTemporaryDir override), and no other test binary in this repo touches
// that path (it carries no RESOURCE_LOCK, unlike the shared ini file). That
// makes it safe to substitute a directory for the database file for the
// lifetime of one test: replaces any file there with an empty directory on
// construction (so LyricStore::open()'s QSqlDatabase::open() fails) and
// puts the path back to a clean slate on destruction, so later tests in
// this binary see a fresh database again.
class ScopedDatabasePathIsADirectory
{
public:
    ScopedDatabasePathIsADirectory()
        : m_path(PlasmaLyrics::LyricStore::defaultPath())
    {
        QFile::remove(m_path);
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        QDir().mkdir(m_path);
    }

    ~ScopedDatabasePathIsADirectory()
    {
        QDir(m_path).removeRecursively();
    }

private:
    QString m_path;
};

// Mirrors daemon/tests/tst_controlservice.cpp's helper of the same shape,
// extended to also record severity (tst_backendconfig.cpp keeps its own
// copy too -- one per test file is the existing practice here).
QList<QPair<QtMsgType, QString>> *capturedMessages = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedMessages && (type == QtInfoMsg || type == QtWarningMsg)) {
        capturedMessages->append({type, message});
    }
}

class MessageCapture
{
public:
    MessageCapture()
        : m_previous(qInstallMessageHandler(captureMessages))
    {
        capturedMessages = &m_messages;
    }

    ~MessageCapture()
    {
        capturedMessages = nullptr;
        qInstallMessageHandler(m_previous);
    }

    const QList<QPair<QtMsgType, QString>> &messages() const { return m_messages; }

private:
    QList<QPair<QtMsgType, QString>> m_messages;
    QtMessageHandler m_previous;
};

} // namespace

class FakeOffsetControl final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public:
    struct ConfigChangeCall {
        QString store;
        QString applet;
        QString form;
        QString key;
        QString oldValue;
        QString newValue;
    };

    int refreshCount = 0;
    QList<ConfigChangeCall> configChanges;
    // Records both NoteConfigChange and RefreshGlobalOffset calls in
    // arrival order, so a test can prove the change notes go out before
    // the daemon is told to republish the effective value.
    QStringList callOrder;

public Q_SLOTS:
    QString RefreshGlobalOffset()
    {
        ++refreshCount;
        callOrder.append(QStringLiteral("RefreshGlobalOffset"));
        return {};
    }

    void NoteConfigChange(const QString &store, const QString &applet, const QString &form,
                         const QString &key, const QString &oldValue, const QString &newValue)
    {
        configChanges.append({store, applet, form, key, oldValue, newValue});
        callOrder.append(QStringLiteral("NoteConfigChange:%1").arg(key));
    }
};

class GlobalConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void successfulSaveAsksDaemonToRepublishSnapshots()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeOffsetControl control;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &control, QDBusConnection::ExportAllSlots));

        GlobalConfig config;
        config.setEnabled(!config.enabled());
        config.setOffsetMs(321);
        QVERIFY(config.save());
        QTRY_COMPARE(control.refreshCount, 1);

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    // DESIGN.md decision 75: save() reports the two keys the offset config
    // page owns, only where the value actually changed.
    void savingReportsConfigChangeLinesForBothKeys()
    {
        GlobalConfig config;
        const bool oldEnabled = config.enabled();
        const int oldOffsetMs = config.offsetMs();
        config.setEnabled(!oldEnabled);
        config.setOffsetMs(oldOffsetMs == 500 ? 250 : 500);

        MessageCapture capture;
        QVERIFY(config.save());

        QStringList changeLines;
        for (const auto &entry : capture.messages()) {
            if (entry.second.startsWith(QStringLiteral("config changed"))) {
                changeLines.append(entry.second);
            }
        }
        QCOMPARE(changeLines.size(), 2);
        QVERIFY(changeLines.contains(PlasmaLyrics::configChangedLine(
            {.store = QStringLiteral("db"),
             .key = QStringLiteral("globalOffsetEnabled"),
             .oldValue = PlasmaLyrics::renderConfigValue(oldEnabled),
             .newValue = PlasmaLyrics::renderConfigValue(!oldEnabled)})));
        QVERIFY(changeLines.contains(PlasmaLyrics::configChangedLine(
            {.store = QStringLiteral("db"),
             .key = QStringLiteral("globalOffsetMs"),
             .oldValue = QString::number(oldOffsetMs),
             .newValue = QString::number(config.offsetMs())})));
    }

    void noOpSaveReportsNoChangeLines()
    {
        GlobalConfig config;
        QVERIFY(config.save());

        MessageCapture capture;
        QVERIFY(config.save());
        for (const auto &entry : capture.messages()) {
            QVERIFY(!entry.second.startsWith(QStringLiteral("config changed")));
        }
    }

    // Forwarding, and proof that both NoteConfigChange calls reach the
    // daemon before RefreshGlobalOffset -- a listener must see "what
    // changed" before "the effective value changed".
    void forwardsConfigChangesBeforeRequestingRefresh()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeOffsetControl control;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &control, QDBusConnection::ExportAllSlots));

        GlobalConfig config;
        const bool oldEnabled = config.enabled();
        const int oldOffsetMs = config.offsetMs();
        config.setEnabled(!oldEnabled);
        config.setOffsetMs(oldOffsetMs == 500 ? 250 : 500);
        QVERIFY(config.save());

        QTRY_COMPARE(control.refreshCount, 1);
        QCOMPARE(control.configChanges.size(), 2);
        bool sawEnabled = false;
        bool sawOffset = false;
        for (const auto &call : control.configChanges) {
            QCOMPARE(call.store, QStringLiteral("db"));
            QVERIFY(call.applet.isEmpty());
            QVERIFY(call.form.isEmpty());
            if (call.key == QStringLiteral("globalOffsetEnabled")) {
                sawEnabled = true;
                QCOMPARE(call.oldValue, PlasmaLyrics::renderConfigValue(oldEnabled));
                QCOMPARE(call.newValue, PlasmaLyrics::renderConfigValue(!oldEnabled));
            } else if (call.key == QStringLiteral("globalOffsetMs")) {
                sawOffset = true;
                QCOMPARE(call.oldValue, QString::number(oldOffsetMs));
                QCOMPARE(call.newValue, QString::number(config.offsetMs()));
            }
        }
        QVERIFY(sawEnabled);
        QVERIFY(sawOffset);

        QCOMPARE(control.callOrder.size(), 3);
        QVERIFY(control.callOrder.at(0).startsWith(QStringLiteral("NoteConfigChange:")));
        QVERIFY(control.callOrder.at(1).startsWith(QStringLiteral("NoteConfigChange:")));
        QCOMPARE(control.callOrder.at(2), QStringLiteral("RefreshGlobalOffset"));

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    void storeOpenFailureReportsDbOpenFailedAndDoesNotRefresh()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeOffsetControl control;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &control, QDBusConnection::ExportAllSlots));
        ScopedDatabasePathIsADirectory blockStorePath;

        GlobalConfig config;
        MessageCapture capture;
        QVERIFY(!config.save());
        QCOMPARE(control.refreshCount, 0);
        QVERIFY(control.configChanges.isEmpty());

        QCOMPARE(capture.messages().size(), 1);
        QCOMPARE(capture.messages().first().first, QtWarningMsg);
        QCOMPARE(capture.messages().first().second,
                 PlasmaLyrics::configSaveFailedLine(QStringLiteral("db"),
                                                    QStringLiteral("db-open-failed")));

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }
};

QTEST_GUILESS_MAIN(GlobalConfigTest)
#include "tst_globalconfig.moc"
