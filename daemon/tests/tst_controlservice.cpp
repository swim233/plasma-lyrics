#include "daemon/src/controlservice.h"

#include "core/store/lyricstore.h"
#include "daemon/src/resolver.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <algorithm>

using namespace PlasmaLyrics;

namespace {

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
        const TrackRef currentRef{QStringLiteral("netease"), QStringLiteral("id"), 1.0};
        ControlService service(store, resolver, [&] { return std::optional<MprisState>(current); },
                               [&](const MprisState &) { ++requests; },
                               [&] { return std::optional<TrackRef>(currentRef); },
                               [&] { ++publications; });
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(ControlService::serviceName()));
        QVERIFY(bus.registerObject(ControlService::objectPath(), &service,
                                   QDBusConnection::ExportAllSlots));
        QDBusInterface interface(ControlService::serviceName(), ControlService::objectPath(),
                                 ControlService::interfaceName(), bus);
        QVERIFY(interface.isValid());

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

        QDBusReply<QString> clear = interface.call(
            QStringLiteral("ClearPreferredProvider"), current.fingerprint);
        QVERIFY(clear.isValid());
        QCOMPARE(clear.value(), QString());
        QVERIFY(!store.preferredProvider(current.fingerprint));
        QCOMPARE(requests, 3);

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

        QVERIFY(store.setGlobalOffsetEnabled(true));
        QList<QDBusPendingCallWatcher *> pending;
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
        }
        const auto allFinished = [&pending] {
            const auto finished = [](const auto *call) { return call->isFinished(); };
            return std::all_of(pending.cbegin(), pending.cend(), finished);
        };
        QTRY_VERIFY_WITH_TIMEOUT(allFinished(), 5000);
        for (auto *call : pending) {
            const QDBusPendingReply<QString> reply = *call;
            QVERIFY(reply.isValid());
            QCOMPARE(reply.value(), QString());
            call->deleteLater();
        }
        QVERIFY(!store.preferredProvider(current.fingerprint));
        QCOMPARE(store.globalOffsetMs(), 0);
        QCOMPARE(requests, 63);
        QCOMPARE(publications, 64);
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
        current.fingerprint = QStringLiteral("mediaSrc:write-failure");
        current.title = QStringLiteral("song");
        int requests = 0;
        ControlService service(store, resolver, [&] { return std::optional<MprisState>(current); },
                               [&](const MprisState &) { ++requests; });
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

            QCOMPARE(service.SetPreferredProvider(current.fingerprint, amll.id()),
                     QStringLiteral("preference-save-failed"));
            QCOMPARE(requests, 0);
            QVERIFY(!store.preferredProvider(current.fingerprint).has_value());
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }
};

QTEST_GUILESS_MAIN(ControlServiceTest)
#include "tst_controlservice.moc"
