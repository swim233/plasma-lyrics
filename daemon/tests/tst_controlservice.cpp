#include "daemon/src/controlservice.h"

#include "core/store/lyricstore.h"
#include "daemon/src/resolver.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <algorithm>

using namespace PlasmaLyrics;

namespace {

struct CapturedMessage
{
    QtMsgType type;
    QString text;
};

QList<CapturedMessage> *capturedMessages = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedMessages && (type == QtInfoMsg || type == QtWarningMsg)) {
        capturedMessages->append({type, message});
    }
}

// Mirrors tst_resolver.cpp's helper of the same name; extended with the
// message's QtMsgType so tests can assert severity (info vs. warning), not
// just text, for the Note* methods below.
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

    // Returns a fresh copy on every call, so iterators taken from two
    // separate calls belong to different lists: bind it to a local first.
    QStringList messages() const
    {
        QStringList result;
        result.reserve(m_messages.size());
        for (const auto &message : m_messages) {
            result.append(message.text);
        }
        return result;
    }

    bool containsAt(QtMsgType type, const QString &text) const
    {
        return std::any_of(m_messages.cbegin(), m_messages.cend(),
                           [type, &text](const CapturedMessage &message) {
            return message.type == type && message.text == text;
        });
    }

private:
    QList<CapturedMessage> m_messages;
    QtMessageHandler m_previous;
};

class TestProvider final : public Provider
{
public:
    explicit TestProvider(QString id) : m_id(std::move(id)) {}
    QString id() const override { return m_id; }
    bool isConfigured() const override { return true; }
    void search(const TrackQuery &query, SearchCallback callback) override
    {
        callback({{{QStringLiteral("id"), query.title, query.artists, query.album,
                    query.lengthMs}}, {}, false});
    }
    void fetch(const QString &, FetchCallback callback) override
    {
        callback({LyricDocument{{{0, 1000, QStringLiteral("line")}}}, {}, false});
    }
private:
    QString m_id;
};

} // namespace

class ControlServiceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(ControlService::objectPath());
        bus.unregisterService(ControlService::serviceName());
    }

    // DESIGN.md decision 79: the menu's offset commands change the current
    // song's own offset in global mode too, so they need the same song and
    // lyric ref as in per-track mode, and never touch the global value.
    void offsetCommandsChangeOnlyTheSongsOwnOffsetInGlobalMode()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        QVERIFY(store.setGlobalOffsetMs(2000));
        TestProvider provider(QStringLiteral("local"));
        Resolver resolver(store, {&provider});
        int publications = 0;
        std::optional<MprisState> current;
        std::optional<TrackRef> currentRef;
        ControlService service(store, resolver, [&] { return current; },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {},
                               [&] { return currentRef; }, [&] { ++publications; });

        QCOMPARE(service.AdjustOffset(QString(), 500), QStringLiteral("no-current-song"));
        QCOMPARE(service.ResetOffset(QString()), QStringLiteral("no-current-song"));

        current = MprisState{};
        current->fingerprint = QStringLiteral("mediaSrc:current");
        QCOMPARE(service.AdjustOffset(QString(), 500), QStringLiteral("song-changed"));
        QCOMPARE(service.AdjustOffset(QStringLiteral("mediaSrc:old"), 500),
                 QStringLiteral("song-changed"));
        QCOMPARE(service.AdjustOffset(current->fingerprint, 500), QStringLiteral("no-track-ref"));
        QCOMPARE(service.ResetOffset(current->fingerprint), QStringLiteral("no-track-ref"));
        QCOMPARE(publications, 0);

        currentRef = TrackRef{QStringLiteral("local"), QStringLiteral("song"), 1.0};
        QCOMPARE(service.AdjustOffset(current->fingerprint, 500), QString());
        QCOMPARE(service.AdjustOffset(current->fingerprint, 500), QString());
        QCOMPARE(store.offset(*currentRef), 1000);
        QCOMPARE(service.ResetOffset(current->fingerprint), QString());
        QCOMPARE(store.offset(*currentRef), 0);
        QCOMPARE(publications, 3);
        QCOMPARE(store.globalOffsetMs(), 2000);
    }

    void reportsBuildCapabilitiesOutsideTheEnabledResolverChain()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        MprisState current;
        current.music = true;
        current.fingerprint = QStringLiteral("mediaSrc:supported-providers");
        current.title = QStringLiteral("song");
        ControlService service(
            store, resolver, [&] { return std::optional<MprisState>(current); },
            [](const MprisState &, const QString &, ControlService::CachePolicy) {}, {}, {},
            {QStringLiteral("local"), QStringLiteral("netease"), QStringLiteral("amll")});

        QCOMPARE(service.AvailableProviders(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
        QCOMPARE(service.SetPreferredProvider(current.fingerprint, QStringLiteral("amll")),
                 QStringLiteral("provider-unavailable"));
    }

    void validatesFingerprintAndPersistsCommands()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider netease(QStringLiteral("netease"));
        TestProvider amll(QStringLiteral("amll"));
        Resolver resolver(store, {&netease, &amll});
        MprisState current;
        current.music = true;
        current.fingerprint = QStringLiteral("mediaSrc:current");
        current.title = QStringLiteral("song");
        current.artists = {QStringLiteral("artist")};
        int requests = 0;
        int publications = 0;
        QStringList triggers;
        QList<ControlService::CachePolicy> policies;
        const TrackRef currentRef{QStringLiteral("netease"), QStringLiteral("id"), 1.0};
        ControlService service(store, resolver, [&] { return std::optional<MprisState>(current); },
                               [&](const MprisState &, const QString &trigger,
                                   ControlService::CachePolicy cache) {
                                   ++requests;
                                   triggers.append(trigger);
                                   policies.append(cache);
                               },
                               [&] { return std::optional<TrackRef>(currentRef); },
                               [&] { ++publications; });
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(ControlService::serviceName()));
        QVERIFY(bus.registerObject(ControlService::objectPath(), &service,
                                   QDBusConnection::ExportAllSlots));
        QDBusInterface interface(ControlService::serviceName(), ControlService::objectPath(),
                                 ControlService::interfaceName(), bus);
        QVERIFY(interface.isValid());

        QDBusReply<QStringList> providers = interface.call(QStringLiteral("AvailableProviders"));
        QVERIFY(providers.isValid());
        QCOMPARE(providers.value(),
                 QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));

        QDBusReply<QString> mismatch = interface.call(
            QStringLiteral("SetPreferredProvider"), QStringLiteral("old"), QStringLiteral("amll"));
        QVERIFY(mismatch.isValid());
        QVERIFY(!mismatch.value().isEmpty());
        QVERIFY(!store.preferredProvider(current.fingerprint));
        QCOMPARE(requests, 0);

        QDBusReply<QString> set = interface.call(
            QStringLiteral("SetPreferredProvider"), current.fingerprint, QStringLiteral("amll"));
        QVERIFY(set.isValid());
        QCOMPARE(set.value(), QString());
        QCOMPARE(store.preferredProvider(current.fingerprint),
                 std::optional<QString>(QStringLiteral("amll")));
        QCOMPARE(requests, 1);
        QCOMPARE(triggers.last(), QStringLiteral("set-preferred"));
        QCOMPARE(policies.last(), ControlService::CachePolicy::PreferCached);

        QDBusReply<QString> unavailable = interface.call(
            QStringLiteral("SetPreferredProvider"), current.fingerprint, QStringLiteral("missing"));
        QVERIFY(unavailable.isValid());
        QVERIFY(!unavailable.value().isEmpty());
        QCOMPARE(requests, 1);

        QDBusReply<QString> research = interface.call(
            QStringLiteral("Research"), current.fingerprint);
        QVERIFY(research.isValid());
        QCOMPARE(research.value(), QString());
        QCOMPARE(requests, 2);
        QCOMPARE(triggers.last(), QStringLiteral("research"));
        QCOMPARE(policies.last(), ControlService::CachePolicy::Bypass);

        QDBusReply<QString> clear = interface.call(
            QStringLiteral("ClearPreferredProvider"), current.fingerprint);
        QVERIFY(clear.isValid());
        QCOMPARE(clear.value(), QString());
        QVERIFY(!store.preferredProvider(current.fingerprint));
        QCOMPARE(requests, 3);
        QCOMPARE(triggers.last(), QStringLiteral("clear-preferred"));
        QCOMPARE(policies.last(), ControlService::CachePolicy::PreferCached);

        QDBusReply<QString> adjustOne = interface.call(
            QStringLiteral("AdjustOffset"), current.fingerprint, 500);
        QVERIFY(adjustOne.isValid());
        QCOMPARE(adjustOne.value(), QString());
        QDBusReply<QString> adjustTwo = interface.call(
            QStringLiteral("AdjustOffset"), current.fingerprint, 500);
        QCOMPARE(adjustTwo.value(), QString());
        QCOMPARE(store.offset(currentRef), 1000);
        QCOMPARE(publications, 2);

        QDBusReply<QString> staleAdjust = interface.call(
            QStringLiteral("AdjustOffset"), QStringLiteral("old"), 500);
        QCOMPARE(staleAdjust.value(), QStringLiteral("song-changed"));
        QCOMPARE(store.offset(currentRef), 1000);

        QDBusReply<QString> reset = interface.call(
            QStringLiteral("ResetOffset"), current.fingerprint);
        QCOMPARE(reset.value(), QString());
        QCOMPARE(store.offset(currentRef), 0);
        QCOMPARE(publications, 3);

        QDBusReply<QString> refresh = interface.call(QStringLiteral("RefreshGlobalOffset"));
        QVERIFY(refresh.isValid());
        QCOMPARE(refresh.value(), QString());
        QCOMPARE(publications, 4);
    }

    void allMethodsCompleteUnderConcurrentDbusLoad()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        TestProvider netease(QStringLiteral("netease"));
        TestProvider amll(QStringLiteral("amll"));
        Resolver resolver(store, {&netease, &amll});
        MprisState current;
        current.music = true;
        current.fingerprint = QStringLiteral("mediaSrc:stress");
        current.title = QStringLiteral("song");
        int requests = 0;
        int publications = 0;
        QHash<QString, int> triggerCounts;
        QHash<QString, int> preferCachedCounts;
        const TrackRef currentRef{QStringLiteral("netease"), QStringLiteral("id"), 1.0};
        ControlService service(store, resolver, [&] { return std::optional<MprisState>(current); },
                               [&](const MprisState &, const QString &trigger,
                                   ControlService::CachePolicy cache) {
                                   ++requests;
                                   ++triggerCounts[trigger];
                                   if (cache == ControlService::CachePolicy::PreferCached) {
                                       ++preferCachedCounts[trigger];
                                   }
                               },
                               [&] { return std::optional<TrackRef>(currentRef); },
                               [&] { ++publications; });
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(ControlService::serviceName()));
        QVERIFY(bus.registerObject(ControlService::objectPath(), &service,
                                   QDBusConnection::ExportAllSlots));
        QDBusInterface interface(ControlService::serviceName(), ControlService::objectPath(),
                                 ControlService::interfaceName(), bus);
        QVERIFY(interface.isValid());

        QList<QDBusPendingCallWatcher *> pending;
        QList<QDBusPendingCallWatcher *> providerQueries;
        for (int i = 0; i < 20; ++i) {
            pending.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("SetPreferredProvider"), current.fingerprint,
                QStringLiteral("amll")), this));
            pending.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("ClearPreferredProvider"), current.fingerprint), this));
            pending.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("Research"), current.fingerprint), this));
            pending.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("AdjustOffset"), current.fingerprint, 1), this));
            pending.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("ResetOffset"), current.fingerprint), this));
            pending.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("RefreshGlobalOffset")), this));
            providerQueries.append(new QDBusPendingCallWatcher(interface.asyncCall(
                QStringLiteral("AvailableProviders")), this));
        }
        const auto allFinished = [&pending, &providerQueries] {
            const auto finished = [](const auto *call) { return call->isFinished(); };
            return std::all_of(pending.cbegin(), pending.cend(), finished)
                && std::all_of(providerQueries.cbegin(), providerQueries.cend(), finished);
        };
        QTRY_VERIFY_WITH_TIMEOUT(allFinished(), 5000);
        for (auto *call : pending) {
            const QDBusPendingReply<QString> reply = *call;
            QVERIFY(reply.isValid());
            QCOMPARE(reply.value(), QString());
            call->deleteLater();
        }
        for (auto *call : providerQueries) {
            const QDBusPendingReply<QStringList> reply = *call;
            QVERIFY(reply.isValid());
            QCOMPARE(reply.value(),
                     QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));
            call->deleteLater();
        }
        QVERIFY(!store.preferredProvider(current.fingerprint));
        // Offset commands in global mode write only the song's own offset,
        // whose final value depends on dispatch order.
        QCOMPARE(store.globalOffsetMs(), 0);
        QCOMPARE(requests, 60);
        QCOMPARE(publications, 60);
        // 20 iterations x {SetPreferredProvider, ClearPreferredProvider,
        // Research}; order is not guaranteed under concurrent dispatch, so
        // assert per-trigger counts instead of a sequence.
        QCOMPARE(triggerCounts.value(QStringLiteral("set-preferred")), 20);
        QCOMPARE(triggerCounts.value(QStringLiteral("clear-preferred")), 20);
        QCOMPARE(triggerCounts.value(QStringLiteral("research")), 20);
        QCOMPARE(preferCachedCounts.value(QStringLiteral("set-preferred")), 20);
        QCOMPARE(preferCachedCounts.value(QStringLiteral("clear-preferred")), 20);
        QCOMPARE(preferCachedCounts.value(QStringLiteral("research")), 0);
    }

    void preferenceWriteFailureIsReturnedWithoutStartingAResolve()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider amll(QStringLiteral("amll"));
        Resolver resolver(store, {&amll});
        MprisState current;
        current.music = true;
        current.fingerprint = QStringLiteral("mediaSrc:write failure");
        current.title = QStringLiteral("song");
        int requests = 0;
        ControlService service(store, resolver, [&] { return std::optional<MprisState>(current); },
                               [&](const MprisState &, const QString &,
                                   ControlService::CachePolicy) { ++requests; });
        const QString connectionName = QStringLiteral("control-fault-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(store.path());
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_preference BEFORE INSERT ON track_preference "
                "BEGIN SELECT RAISE(FAIL, 'preference rejected'); END")));

            MessageCapture capture;
            QCOMPARE(service.SetPreferredProvider(current.fingerprint, amll.id()),
                     QStringLiteral("preference-save-failed"));
            QCOMPARE(requests, 0);
            QVERIFY(!store.preferredProvider(current.fingerprint).has_value());
            const QStringList messages = capture.messages();
            QVERIFY(std::any_of(messages.cbegin(), messages.cend(),
                                [](const QString &message) {
                return message.contains(QStringLiteral("control SetPreferredProvider"))
                    && message.contains(QStringLiteral("result=preference-save-failed"));
            }));
            QVERIFY(std::any_of(messages.cbegin(), messages.cend(),
                                [](const QString &message) {
                return message.contains(QStringLiteral("fingerprint=\"mediaSrc:write failure\""));
            }));
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void noteConfigChangeLogsWithoutAppletContext()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        ControlService service(store, resolver, [] { return std::optional<MprisState>(); },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {});

        MessageCapture capture;
        service.NoteConfigChange(QStringLiteral("ini"), QString(), QString(),
                                 QStringLiteral("providers/enabled"),
                                 QStringLiteral("local,netease"),
                                 QStringLiteral("local,netease,qq"));
        QVERIFY(capture.containsAt(QtInfoMsg,
            QStringLiteral("config changed store=ini key=providers/enabled "
                          "old=\"local,netease\" new=\"local,netease,qq\"")));
    }

    void noteConfigChangeLogsAppletContext()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        ControlService service(store, resolver, [] { return std::optional<MprisState>(); },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {});

        MessageCapture capture;
        service.NoteConfigChange(QStringLiteral("applet"), QStringLiteral("12"),
                                 QStringLiteral("desktop"), QStringLiteral("desktopFontSize"),
                                 QStringLiteral("34"), QStringLiteral("36"));
        QVERIFY(capture.containsAt(QtInfoMsg,
            QStringLiteral("config changed store=applet applet=12 form=desktop "
                          "key=desktopFontSize old=\"34\" new=\"36\"")));
    }

    void noteSaveFailedLogsAtWarning()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        ControlService service(store, resolver, [] { return std::optional<MprisState>(); },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {});

        MessageCapture capture;
        service.NoteSaveFailed(QStringLiteral("db"), QStringLiteral("disk full"));
        QVERIFY(capture.containsAt(QtWarningMsg,
            QStringLiteral("config save failed store=db reason=\"disk full\"")));
    }

    void noteRestartRequestedLogsAtInfo()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        ControlService service(store, resolver, [] { return std::optional<MprisState>(); },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {});

        MessageCapture capture;
        service.NoteRestartRequested();
        QVERIFY(capture.containsAt(QtInfoMsg, QStringLiteral("config restart requested")));
    }

    void noteRestartFinishedLogsResultAndSeverity()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        ControlService service(store, resolver, [] { return std::optional<MprisState>(); },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {});

        {
            MessageCapture capture;
            service.NoteRestartFinished(true, QString());
            QVERIFY(capture.containsAt(QtInfoMsg,
                QStringLiteral("config restart finished result=ok")));
        }
        {
            MessageCapture capture;
            service.NoteRestartFinished(false,
                                        QStringLiteral("restart command exited with code 1"));
            QVERIFY(capture.containsAt(QtWarningMsg,
                QStringLiteral("config restart finished result=failed "
                              "error=\"restart command exited with code 1\"")));
        }
    }

    void notesAreExportedOverTheSessionBus()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider local(QStringLiteral("local"));
        Resolver resolver(store, {&local});
        ControlService service(store, resolver, [] { return std::optional<MprisState>(); },
                               [](const MprisState &, const QString &,
                                  ControlService::CachePolicy) {});
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(ControlService::serviceName()));
        QVERIFY(bus.registerObject(ControlService::objectPath(), &service,
                                   QDBusConnection::ExportAllSlots));
        QDBusInterface interface(ControlService::serviceName(), ControlService::objectPath(),
                                 ControlService::interfaceName(), bus);
        QVERIFY(interface.isValid());

        MessageCapture capture;
        QDBusReply<void> changeCall = interface.call(
            QStringLiteral("NoteConfigChange"), QStringLiteral("ini"), QString(), QString(),
            QStringLiteral("providers/enabled"), QStringLiteral("local"),
            QStringLiteral("local,qq"));
        QVERIFY(changeCall.isValid());
        QVERIFY(capture.containsAt(QtInfoMsg,
            QStringLiteral("config changed store=ini key=providers/enabled "
                          "old=\"local\" new=\"local,qq\"")));

        QDBusReply<void> restartCall = interface.call(QStringLiteral("NoteRestartRequested"));
        QVERIFY(restartCall.isValid());
        QVERIFY(capture.containsAt(QtInfoMsg, QStringLiteral("config restart requested")));
    }
};

QTEST_GUILESS_MAIN(ControlServiceTest)
#include "tst_controlservice.moc"
