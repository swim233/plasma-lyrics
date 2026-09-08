#include "daemon/src/mpris/mprisplayer.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QEventLoop>
#include <QPointer>
#include <QTest>
#include <QTimer>

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
    double rate() const { return 1.0; }
    qlonglong position() const { return m_position; }
    QVariantMap metadata() const { return m_metadata; }

    void setPosition(qlonglong position) { m_position = position; }

private:
    qlonglong m_position = 12000000;
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
};

QTEST_GUILESS_MAIN(MprisPlayerLifecycleTest)

#include "tst_mprisplayer_lifecycle.moc"
