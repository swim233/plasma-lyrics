#include "daemon/src/mpris/mprispolicy.h"
#include "core/match/matcher.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace PlasmaLyrics;

class MprisPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void replaysRecordedPbiDirtyData()
    {
        QFile file(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR "/pbi-dirty-replay.json"));
        QVERIFY2(file.open(QIODevice::ReadOnly), "recorded MPRIS fixture was not found");
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        const PolicyConfig config{{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")},
                                  {QStringLiteral("https://music.163.com/")}, true};

        QList<MprisState> states;
        for (const auto &value : root.value(QStringLiteral("services")).toArray()) {
            const auto recorded = value.toObject();
            const auto metadata = recorded.value(QStringLiteral("metadata")).toObject();
            MprisState state;
            state.service = recorded.value(QStringLiteral("service")).toString();
            state.playingSerial = recorded.value(QStringLiteral("playingSerial")).toInteger();
            state.playbackStatus = QStringLiteral("Playing");
            state.trackId = metadata.value(QStringLiteral("mpris:trackid")).toString();
            state.lengthUs = metadata.value(QStringLiteral("mpris:length")).toInteger();
            state.title = metadata.value(QStringLiteral("xesam:title")).toString();
            for (const auto &artist : metadata.value(QStringLiteral("xesam:artist")).toArray()) {
                state.artists.append(artist.toString());
            }
            state.album = metadata.value(QStringLiteral("xesam:album")).toString();
            state.url = metadata.value(QStringLiteral("xesam:url")).toString();
            state.mediaSrc = metadata.value(QStringLiteral("kde:mediaSrc")).toString();
            state.kdePid = metadata.value(QStringLiteral("kde:pid")).toInteger();
            state.fingerprint = MprisPolicy::fingerprint(state);
            states.append(state);
        }

        QCOMPARE(states.size(), 2);
        QCOMPARE(states.last().trackId, QStringLiteral("/org/kde/plasma/browser_integration/1337"));
        QCOMPARE(cleanArtists(states.last().artists).size(), 4);
        QCOMPARE(states.last().fingerprint, QStringLiteral("mediaSrc:c739729316bedb013393e5a6c543223f"));
        QCOMPARE(MprisPolicy::choosePlayer(states, QString(), config), states.last().service);

        MprisState pbi = states.last();
        for (const auto &value : root.value(QStringLiteral("metadataEvents")).toArray()) {
            const auto event = value.toObject();
            pbi.url = event.value(QStringLiteral("url")).toString();
            QCOMPARE(MprisPolicy::isMusic(pbi, config), event.value(QStringLiteral("music")).toBool());
        }
        for (const auto &value : root.value(QStringLiteral("positionSamples")).toArray()) {
            const auto sample = value.toObject();
            QCOMPARE(MprisPolicy::isPositionJump(sample.value(QStringLiteral("previousUs")).toInteger(),
                                                  sample.value(QStringLiteral("previousNs")).toInteger(),
                                                  sample.value(QStringLiteral("positionUs")).toInteger(),
                                                  sample.value(QStringLiteral("nowNs")).toInteger(),
                                                  1.0, QStringLiteral("Playing")),
                     sample.value(QStringLiteral("jump")).toBool());
        }
        QCOMPARE(root.value(QStringLiteral("signals")).toObject()
                     .value(QStringLiteral("Seeked")).toInt(), 0);
    }

    void constantTrackIdDoesNotHideTrackChange()
    {
        MprisState first;
        first.trackId = QStringLiteral("/org/kde/plasma/browser_integration/1337");
        first.mediaSrc = QStringLiteral("https://example/c739729316bedb013393e5a6c543223f.mp3");
        MprisState second = first;
        second.mediaSrc = QStringLiteral("https://example/different.mp3");
        QCOMPARE(MprisPolicy::fingerprint(first), QStringLiteral("mediaSrc:c739729316bedb013393e5a6c543223f"));
        QVERIFY(MprisPolicy::fingerprint(first) != MprisPolicy::fingerprint(second));
    }

    void pbiSuppressesBrokenChromiumTwin()
    {
        PolicyConfig config{{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")},
                            {QStringLiteral("https://music.163.com/")}, true};
        MprisState chromium;
        chromium.service = QStringLiteral("org.mpris.MediaPlayer2.chromium.instance3893");
        chromium.title = QStringLiteral("网易云音乐");
        chromium.artists = {QString()};
        chromium.lengthUs = 200000000;
        chromium.playbackStatus = QStringLiteral("Playing");
        chromium.playingSerial = 2;
        MprisState pbi = chromium;
        pbi.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        pbi.title = QStringLiteral("老街北");
        pbi.artists = {QStringLiteral("闹闹丶/FFF君")};
        pbi.url = QStringLiteral("https://music.163.com/st/webplayer");
        pbi.playingSerial = 1;
        QCOMPARE(MprisPolicy::choosePlayer({chromium, pbi}, QString(), config), pbi.service);
    }

    void kdePidSuppressesOnlyTheProxiedBrowser()
    {
        PolicyConfig config{{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")},
                            {QStringLiteral("https://music.163.com/")}, true};
        // Unlike the twin above, this native service passes isMusic() on its own,
        // so the outcome is decided by deduplication and not by filtering.
        MprisState native;
        native.service = QStringLiteral("org.mpris.MediaPlayer2.chromium.instance3893");
        native.title = QStringLiteral("春风漫野绿");
        native.artists = {QStringLiteral("洛天依Official")};
        native.lengthUs = 219130438;
        native.playbackStatus = QStringLiteral("Playing");
        native.playingSerial = 2;

        MprisState pbi = native;
        pbi.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        pbi.url = QStringLiteral("https://music.163.com/st/webplayer");
        pbi.playingSerial = 1;

        pbi.kdePid = 3893;
        QCOMPARE(MprisPolicy::choosePlayer({native, pbi}, QString(), config), pbi.service);

        // The integration is proxying some other browser, so the one playing
        // natively is a separate source and has to stay eligible.
        pbi.kdePid = 5150;
        QCOMPARE(MprisPolicy::choosePlayer({native, pbi}, QString(), config), native.service);

        // With no pid to pair them up, prefer the integration over reporting the
        // same track twice.
        pbi.kdePid = 0;
        QCOMPARE(MprisPolicy::choosePlayer({native, pbi}, QString(), config), pbi.service);
    }

    void samePbiServiceCanSwitchToVideo()
    {
        PolicyConfig config{{}, {QStringLiteral("https://music.163.com/")}, true};
        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        state.title = QStringLiteral("video");
        state.artists = {QStringLiteral("uploader")};
        state.lengthUs = 1000000;
        state.url = QStringLiteral("https://www.bilibili.com/video/BV1mkg36zEfX/");
        QVERIFY(!MprisPolicy::isMusic(state, config));
        state.url = QStringLiteral("https://music.163.com/st/webplayer");
        QVERIFY(MprisPolicy::isMusic(state, config));
    }

    void detectsSeekWithoutSeekedSignal()
    {
        QVERIFY(MprisPolicy::isPositionJump(10000000, 1000000000, 3000000, 2000000000, 1.0,
                                            QStringLiteral("Playing")));
        QVERIFY(MprisPolicy::isPositionJump(10000000, 1000000000, 40000000, 2000000000, 1.0,
                                            QStringLiteral("Playing")));
        QVERIFY(!MprisPolicy::isPositionJump(10000000, 1000000000, 11000000, 2000000000, 1.0,
                                             QStringLiteral("Playing")));
    }
};

QTEST_GUILESS_MAIN(MprisPolicyTest)
#include "tst_mprispolicy.moc"
