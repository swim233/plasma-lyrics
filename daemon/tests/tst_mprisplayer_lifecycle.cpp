#include "daemon/src/mpris/mprisplayer.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QEventLoop>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrl>

using namespace PlasmaLyrics;

namespace {

constexpr auto fakeService = "org.mpris.MediaPlayer2.tstlifecycle";
constexpr auto fakePath = "/org/mpris/MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";

class RootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity)

public:
    explicit RootAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    QString identity() const { return QStringLiteral("Lifecycle Test Player"); }
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
    explicit PlayerAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    QString playbackStatus() const { return QStringLiteral("Playing"); }
    double rate() const { return m_rate; }
    qlonglong position() const { return m_position; }
    QVariantMap metadata() const { return m_metadata; }

    void setPosition(qlonglong position) { m_position = position; }
    void setRate(double rate) { m_rate = rate; }
    void setUrl(const QString &url) { m_metadata.insert(QStringLiteral("xesam:url"), url); }

private:
    qlonglong m_position = 12000000;
    double m_rate = 1.0;
    QVariantMap m_metadata{
        {QStringLiteral("mpris:trackid"), QStringLiteral("/fake/constant-track-id")},
        {QStringLiteral("mpris:length"), qlonglong(240000000)},
        {QStringLiteral("xesam:title"), QStringLiteral("Initial Song")},
        {QStringLiteral("xesam:artist"), QStringList{QStringLiteral("Fake Artist")}},
        {QStringLiteral("xesam:album"), QStringLiteral("Fake Album")},
    };
};

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
    void setUrl(const QString &url) { m_player->setUrl(url); }

    bool announceRate(double rate)
    {
        m_player->setRate(rate);
        QDBusMessage signal = QDBusMessage::createSignal(
            QString::fromLatin1(fakePath),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        signal << QString::fromLatin1(playerInterface)
               << QVariant::fromValue(QVariantMap{
                      {QStringLiteral("Rate"), QVariant::fromValue(rate)}})
               << QStringList{};
        return QDBusConnection::sessionBus().send(signal);
    }

    bool announcePosition(qlonglong position)
    {
        setPosition(position);
        QDBusMessage signal = QDBusMessage::createSignal(
            QString::fromLatin1(fakePath),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        signal << QString::fromLatin1(playerInterface)
               << QVariant::fromValue(QVariantMap{
                      {QStringLiteral("Position"), QVariant::fromValue(position)}})
               << QStringList{};
        return QDBusConnection::sessionBus().send(signal);
    }

    bool announceSeeked(qlonglong position)
    {
        setPosition(position);
        QDBusMessage signal = QDBusMessage::createSignal(
            QString::fromLatin1(fakePath), QString::fromLatin1(playerInterface),
            QStringLiteral("Seeked"));
        signal << position;
        return QDBusConnection::sessionBus().send(signal);
    }

private:
    QObject m_object;
    PlayerAdaptor *m_player;
};

struct NestedDeletionResult {
    bool enteredLoop = false;
    bool destroyed = false;
    bool timedOut = false;
};

NestedDeletionResult deleteFromNestedEventLoop(MprisPlayer *player)
{
    NestedDeletionResult result;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(2000);

    QObject::connect(player, &QObject::destroyed, &loop, [&] {
        result.destroyed = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        result.timedOut = true;
        loop.quit();
    });

    const QPointer<MprisPlayer> guard(player);
    // Queue the deletion only after exec() has entered the nested loop. This
    // gives the DeferredDelete event the same loop level as the production
    // failure; posting deleteLater() before exec() would test a different rule.
    QTimer::singleShot(0, &loop, [&, guard] {
        result.enteredLoop = true;
        if (guard) {
            guard->deleteLater();
        }
    });

    timeout.start();
    loop.exec();
    return result;
}

} // namespace

class MprisPlayerLifecycleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QDBusConnection::sessionBus().isConnected(),
                 "this test needs a session bus; run it under dbus-run-session");
    }

    void standardUrlIsLoadedWithoutKdeMediaSrc()
    {
        FakePlayer fake;
        const QString url = QUrl::fromLocalFile(
            QStringLiteral("/tmp/plasma-lyrics-standard-mpris.flac")).toString();
        fake.setUrl(url);
        QVERIFY(fake.announce());

        MprisPlayer player(QString::fromLatin1(fakeService));
        QCOMPARE(player.state().url, url);
        QVERIFY(player.state().mediaSrc.isEmpty());
    }

    // The reported crash: a player leaving the bus deletes MprisPlayer while
    // PropertiesChanged for that same player is still on the stack, and apply()
    // went on to touch this afterwards. The signal travels over the real bus
    // because the reported backtrace crashed underneath QtDBus's own dispatch
    // frames, which a direct invokeMethod() would leave out.
    //
    // Carrying PlaybackStatus rather than the Metadata of the original report
    // is what gives this test teeth. Both reach the same post-emit branch, but
    // a metadata change short-circuits `metadataChanged || oldStatus !=
    // m_state.playbackStatus` before that member read, which leaves the only
    // dangling dereference inside libQt6Core -- code no sanitizer here
    // instruments, so the test would pass against the broken daemon. A status
    // change forces the read to happen in daemon code, where ASan sees it.
    void propertiesChangedHandlerMayDeletePlayer()
    {
        FakePlayer fake;
        QVERIFY(fake.announce());

        auto *player = new MprisPlayer(QString::fromLatin1(fakeService));
        QPointer<MprisPlayer> guard(player);
        bool changedReceived = false;
        NestedDeletionResult deletion;

        connect(player, &MprisPlayer::changed, this,
                [&](bool metadataChanged, bool anchorChanged, bool becamePlaying) {
                    changedReceived = true;
                    QVERIFY(!metadataChanged);
                    QVERIFY(anchorChanged);
                    QVERIFY(!becamePlaying);
                    deletion = deleteFromNestedEventLoop(player);
                },
                Qt::DirectConnection);

        QDBusMessage signal = QDBusMessage::createSignal(QString::fromLatin1(fakePath),
                                                        QStringLiteral("org.freedesktop.DBus.Properties"),
                                                        QStringLiteral("PropertiesChanged"));
        signal << QString::fromLatin1(playerInterface)
               << QVariant::fromValue(QVariantMap{{QStringLiteral("PlaybackStatus"), QStringLiteral("Paused")}})
               << QStringList{};
        QVERIFY(QDBusConnection::sessionBus().send(signal));

        QTRY_VERIFY(changedReceived);
        QTRY_VERIFY(guard.isNull());
        QVERIFY(deletion.enteredLoop);
        QVERIFY(deletion.destroyed);
        QVERIFY(!deletion.timedOut);
    }

    void pollPositionHandlerMayDeletePlayer()
    {
        FakePlayer fake;
        QVERIFY(fake.announce());

        auto *player = new MprisPlayer(QString::fromLatin1(fakeService));
        QPointer<MprisPlayer> guard(player);
        fake.setPosition(2000000);

        bool changedReceived = false;
        NestedDeletionResult deletion;
        connect(player, &MprisPlayer::changed, this,
                [&](bool metadataChanged, bool anchorChanged, bool becamePlaying) {
                    changedReceived = true;
                    QVERIFY(!metadataChanged);
                    QVERIFY(anchorChanged);
                    QVERIFY(!becamePlaying);
                    QCOMPARE(player->state().positionUs, 2000000);
                    deletion = deleteFromNestedEventLoop(player);
                },
                Qt::DirectConnection);

        player->pollPosition();

        QVERIFY(changedReceived);
        QVERIFY(deletion.enteredLoop);
        QVERIFY(deletion.destroyed);
        QVERIFY(!deletion.timedOut);
        QVERIFY(guard.isNull());
    }

    void rateOnlyChangeAdvancesOldAnchorAndPublishesNewAnchor()
    {
        FakePlayer fake;
        fake.setPosition(12000000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy changes(&player, &MprisPlayer::changed);
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        now = 3000000000;
        QVERIFY(fake.announceRate(2.0));
        QTRY_COMPARE(player.state().rate, 2.0);

        QCOMPARE(player.state().positionUs, 14000000);
        QCOMPARE(player.state().anchorMonotonicNs, now);
        QCOMPARE(changes.size(), 1);
        QVERIFY(!changes.first().at(0).toBool());
        QVERIFY(changes.first().at(1).toBool());
        QVERIFY(!changes.first().at(2).toBool());
        QCOMPARE(rounds.size(), 0);
    }

    void rateOnlyChangeAdvancesFromTheLatestPolledSample()
    {
        FakePlayer fake;
        fake.setPosition(12000000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy changes(&player, &MprisPlayer::changed);

        now = 3000000000;
        fake.setPosition(14000000);
        player.pollPosition();
        QCOMPARE(player.state().positionUs, 14000000);
        QCOMPARE(changes.size(), 0);

        now = 4000000000;
        QVERIFY(fake.announceRate(2.0));
        QTRY_COMPARE(player.state().rate, 2.0);

        QCOMPARE(player.state().positionUs, 15000000);
        QCOMPARE(player.state().anchorMonotonicNs, now);
        QCOMPARE(changes.size(), 1);
        QVERIFY(changes.first().at(1).toBool());
    }

    void ordinaryPollKeepsPositionAndAnchorAsOneSample()
    {
        FakePlayer fake;
        fake.setPosition(12000000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy changes(&player, &MprisPlayer::changed);

        now = 3000000000;
        fake.setPosition(14000000);
        player.pollPosition();
        now = 5000000000;
        fake.setPosition(16000000);
        player.pollPosition();

        QCOMPARE(changes.size(), 0);
        QCOMPARE(player.state().positionUs, 16000000);
        QCOMPARE(player.state().anchorMonotonicNs, now);
        // Any later publisher (manual source switch, offset adjustment, or an
        // asynchronous resolve callback) reads this same cached pair. Its
        // frontend extrapolation must add only time after the latest poll.
        now = 6000000000;
        const qint64 extrapolated = player.state().positionUs
            + (now - player.state().anchorMonotonicNs) / 1000;
        QCOMPARE(extrapolated, 17000000);
    }

    void endToStartWrapEmitsExactlyOnePlaybackRound()
    {
        FakePlayer fake;
        fake.setPosition(239500000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        now = 2000000000;
        fake.setPosition(500000);
        player.pollPosition();
        QCOMPARE(rounds.size(), 1);

        // The next poll samples the same opening position, so the one wrap
        // cannot fan out into repeated provider retries.
        player.pollPosition();
        QCOMPARE(rounds.size(), 1);
    }

    void ordinaryBackwardSeekDoesNotStartAPlaybackRound()
    {
        FakePlayer fake;
        fake.setPosition(120000000);
        QVERIFY(fake.announce());
        MprisPlayer player(QString::fromLatin1(fakeService));
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        fake.setPosition(1000000);
        player.pollPosition();

        QCOMPARE(rounds.size(), 0);
        QCOMPARE(player.state().positionUs, 1000000);
    }

    void endToStartManualSeekThroughPollingDoesNotStartAPlaybackRound()
    {
        FakePlayer fake;
        fake.setPosition(235000000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        // Only 100 ms elapsed while five seconds remained: this is the P2
        // regression's manual seek shape, despite crossing both old windows.
        now = 1100000000;
        fake.setPosition(1000000);
        player.pollPosition();

        QCOMPARE(rounds.size(), 0);
        QCOMPARE(player.state().positionUs, 1000000);
    }

    void positionPropertyWrapEmitsOnePlaybackRound()
    {
        FakePlayer fake;
        fake.setPosition(239500000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        now = 2000000000;
        QVERIFY(fake.announcePosition(500000));
        QTRY_COMPARE(rounds.size(), 1);

        // A following poll observes the already-updated opening sample and
        // must not report the same wrap a second time.
        player.pollPosition();
        QCOMPARE(rounds.size(), 1);
    }

    void endToStartManualSeekThroughPositionPropertyDoesNotStartRound()
    {
        FakePlayer fake;
        fake.setPosition(235000000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        now = 1100000000;
        QVERIFY(fake.announcePosition(1000000));
        QTRY_COMPARE(player.state().positionUs, 1000000);
        QCOMPARE(rounds.size(), 0);
    }

    void seekedSignalReanchorsAndSuppressesManualRound()
    {
        FakePlayer fake;
        fake.setPosition(235000000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);
        QSignalSpy changes(&player, &MprisPlayer::changed);

        now = 1100000000;
        QVERIFY(fake.announceSeeked(1000000));
        QTRY_COMPARE(player.state().positionUs, 1000000);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(rounds.size(), 0);

        // Both supported Position observation paths are now based at the seek
        // target and cannot report the same action as a round later.
        now = 1200000000;
        QVERIFY(fake.announcePosition(1100000));
        QTRY_COMPARE(player.state().positionUs, 1100000);
        QCOMPARE(rounds.size(), 0);
        now = 1300000000;
        player.pollPosition();
        QCOMPARE(rounds.size(), 0);
    }

    void naturallyTimedSeekedAtLoopBoundaryEmitsExactlyOnce()
    {
        FakePlayer fake;
        fake.setPosition(239500000);
        QVERIFY(fake.announce());
        qint64 now = 1000000000;
        MprisPlayer player(QString::fromLatin1(fakeService), [&now] { return now; });
        QSignalSpy rounds(&player, &MprisPlayer::playbackRoundStarted);

        // Some implementations emit Seeked for the repeat boundary as well.
        // The timing evidence keeps that real round while re-anchoring prevents
        // its next Position observation from duplicating the event.
        now = 2000000000;
        QVERIFY(fake.announceSeeked(500000));
        QTRY_COMPARE(rounds.size(), 1);
        now = 2100000000;
        player.pollPosition();
        QCOMPARE(rounds.size(), 1);
    }
};

QTEST_GUILESS_MAIN(MprisPlayerLifecycleTest)

#include "tst_mprisplayer_lifecycle.moc"
