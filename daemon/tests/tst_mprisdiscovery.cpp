#include "daemon/src/mpris/mprismanager.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

using namespace PlasmaLyrics;

namespace {

constexpr auto fakeService = "org.mpris.MediaPlayer2.tstfake";
constexpr auto fakePath = "/org/mpris/MediaPlayer2";

class RootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity)

public:
    // An inherited constructor keeps the base's access, and QDBusAbstractAdaptor
    // makes its own protected, so it has to be redeclared to be usable here.
    explicit RootAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}

    QString identity() const { return QStringLiteral("Fake Player"); }
};

class PlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(QVariantMap Metadata READ metadata)

public:
    explicit PlayerAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}

    QString playbackStatus() const { return QStringLiteral("Playing"); }
    double rate() const { return m_rate; }
    qlonglong position() const { return m_position; }
    void setPosition(qlonglong position) { m_position = position; }
    void setRate(double rate) { m_rate = rate; }

    // No xesam:url, so the policy falls through to the metadata heuristic:
    // a title, a real artist and a length are what make this count as music.
    QVariantMap metadata() const
    {
        return {{QStringLiteral("mpris:trackid"), QStringLiteral("/fake/track/1")},
                {QStringLiteral("mpris:length"), qlonglong(240000000)},
                {QStringLiteral("xesam:title"), QStringLiteral("Fake Song")},
                {QStringLiteral("xesam:artist"), QStringList{QStringLiteral("Fake Artist")}},
                {QStringLiteral("xesam:album"), QStringLiteral("Fake Album")}};
    }

private:
    qlonglong m_position = 12000000;
    double m_rate = 1.0;
};

// Owns the exported object and the bus name, so a test can decide exactly when
// the player appears on the bus and when it leaves again.
class FakePlayer
{
public:
    FakePlayer()
        : m_player(new PlayerAdaptor(&m_object))
    {
        new RootAdaptor(&m_object);
    }

    ~FakePlayer() { retire(); }

    bool announce()
    {
        return QDBusConnection::sessionBus().registerObject(QString::fromLatin1(fakePath), &m_object,
                                                            QDBusConnection::ExportAdaptors)
            && QDBusConnection::sessionBus().registerService(QString::fromLatin1(fakeService));
    }

    void retire()
    {
        QDBusConnection::sessionBus().unregisterService(QString::fromLatin1(fakeService));
        QDBusConnection::sessionBus().unregisterObject(QString::fromLatin1(fakePath));
    }

    void setPosition(qlonglong position) { m_player->setPosition(position); }

    bool announceRate(double rate)
    {
        m_player->setRate(rate);
        QDBusMessage signal = QDBusMessage::createSignal(
            QString::fromLatin1(fakePath),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        signal << QStringLiteral("org.mpris.MediaPlayer2.Player")
               << QVariant::fromValue(QVariantMap{
                      {QStringLiteral("Rate"), QVariant::fromValue(rate)}})
               << QStringList{};
        return QDBusConnection::sessionBus().send(signal);
    }

private:
    QObject m_object;
    PlayerAdaptor *m_player;
};

} // namespace

class MprisDiscoveryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QDBusConnection::sessionBus().isConnected(),
                 "this test needs a session bus; run it under dbus-run-session");
    }

    // The regression this file exists for. QDBusServiceWatcher only ever
    // signals for names passed to addWatchedService(), and the manager could
    // only pass the ones already on the bus, so a player that started after
    // the daemon stayed invisible until the daemon was restarted. Since the
    // daemon starts with the session, that was every player.
    void discoversPlayerRegisteredAfterStartup()
    {
        MprisManager manager(PolicyConfig{});
        QVERIFY2(!manager.activeState().has_value(),
                 "the bus must be empty of players before the manager is built, "
                 "or this would pass through the startup enumeration instead");

        QSignalSpy spy(&manager, &MprisManager::activeStateChanged);
        FakePlayer player;
        QVERIFY(player.announce());

        QVERIFY(spy.wait());
        const auto state = manager.activeState();
        QVERIFY(state.has_value());
        QCOMPARE(state->service, QString::fromLatin1(fakeService));
        QCOMPARE(state->title, QStringLiteral("Fake Song"));
        QCOMPARE(state->artists, QStringList{QStringLiteral("Fake Artist")});
        QCOMPARE(state->lengthUs, 240000000);
        QCOMPARE(state->playbackStatus, QStringLiteral("Playing"));
        QVERIFY(state->music);
    }

    void forgetsPlayerThatLeavesTheBus()
    {
        MprisManager manager(PolicyConfig{});
        QSignalSpy spy(&manager, &MprisManager::activeStateChanged);
        FakePlayer player;
        QVERIFY(player.announce());
        QVERIFY(spy.wait());
        QVERIFY(manager.activeState().has_value());

        spy.clear();
        player.retire();
        QVERIFY(spy.wait());
        QVERIFY2(!manager.activeState().has_value(),
                 "a player that closed must not keep the last track on screen");
    }

    void ignoresNonPlayerNames()
    {
        MprisManager manager(PolicyConfig{});
        QSignalSpy spy(&manager, &MprisManager::activeStateChanged);
        // Every name on the bus reaches the NameOwnerChanged slot now, not just
        // the watched ones, so the prefix test is the only thing keeping the
        // manager from probing unrelated services.
        QVERIFY(QDBusConnection::sessionBus().registerService(QStringLiteral("org.example.NotAPlayer")));
        QVERIFY(!spy.wait(500));
        QVERIFY(!manager.activeState().has_value());
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.example.NotAPlayer"));
    }

    void forwardsOneEventForEachEndToStartPlaybackRound()
    {
        MprisManager manager(PolicyConfig{});
        QSignalSpy activeSpy(&manager, &MprisManager::activeStateChanged);
        QSignalSpy roundSpy(&manager, &MprisManager::playbackRoundStarted);
        FakePlayer player;
        player.setPosition(239500000);
        QVERIFY(player.announce());
        QVERIFY(activeSpy.wait());

        player.setPosition(500000);
        QTRY_COMPARE_WITH_TIMEOUT(roundSpy.size(), 1, 2500);
        QTest::qWait(1200);
        QCOMPARE(roundSpy.size(), 1);
    }

    void rateOnlyChangePublishesAnUpdatedActiveState()
    {
        MprisManager manager(PolicyConfig{});
        QSignalSpy activeSpy(&manager, &MprisManager::activeStateChanged);
        QSignalSpy roundSpy(&manager, &MprisManager::playbackRoundStarted);
        FakePlayer player;
        QVERIFY(player.announce());
        QVERIFY(activeSpy.wait());
        const auto before = manager.activeState();
        QVERIFY(before.has_value());

        activeSpy.clear();
        QTest::qWait(20);
        QVERIFY(player.announceRate(1.5));
        QVERIFY(activeSpy.wait());

        const auto after = manager.activeState();
        QVERIFY(after.has_value());
        QCOMPARE(after->rate, 1.5);
        QVERIFY(after->anchorMonotonicNs > before->anchorMonotonicNs);
        QVERIFY(after->positionUs >= before->positionUs);
        QCOMPARE(roundSpy.size(), 0);
    }

    void rateOnlyChangeAfterManagerPollUsesThePolledPositionOnce()
    {
        MprisManager manager(PolicyConfig{});
        QSignalSpy activeSpy(&manager, &MprisManager::activeStateChanged);
        QSignalSpy roundSpy(&manager, &MprisManager::playbackRoundStarted);
        FakePlayer player;
        QVERIFY(player.announce());
        QVERIFY(activeSpy.wait());

        // This is the position expected after roughly one second at Rate=1.
        // Wait until the manager's real timer has observed it without a jump.
        player.setPosition(13000000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.activeState().has_value()
                                     && manager.activeState()->positionUs == 13000000,
                                 2500);

        activeSpy.clear();
        QVERIFY(player.announceRate(2.0));
        QVERIFY(activeSpy.wait());

        const auto after = manager.activeState();
        QVERIFY(after.has_value());
        QCOMPARE(after->rate, 2.0);
        QVERIFY(after->positionUs >= 13000000);
        QVERIFY2(after->positionUs < 13500000,
                 "Rate update must not replay the interval already consumed by polling");
        QCOMPARE(roundSpy.size(), 0);
    }
};

QTEST_GUILESS_MAIN(MprisDiscoveryTest)

#include "tst_mprisdiscovery.moc"
