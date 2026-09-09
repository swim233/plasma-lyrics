#include "daemon/src/controlservice.h"

#include "core/store/lyricstore.h"
#include "daemon/src/resolver.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

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
        ControlService service(store, resolver, [&] { return std::optional<MprisState>(current); },
                               [&](const MprisState &) { ++requests; });
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
