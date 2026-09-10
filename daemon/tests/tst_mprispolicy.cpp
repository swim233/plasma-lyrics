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

    void distinguishesPlaybackRoundFromOrdinarySeek()
    {
        // One second of monotonic playback at 1x consumes the final 500 ms
        // and predicts the observed 500 ms wrapped position.
        QCOMPARE(MprisPolicy::isPlaybackRound(
                     239500000, 1000000000, 500000, 2000000000,
                     240000000, 1.0, QStringLiteral("Playing")), true);
        // The same end-to-start positions reached too early are a seek, not a
        // natural round.
        QCOMPARE(MprisPolicy::isPlaybackRound(
                     235000000, 1000000000, 1000000, 1100000000,
                     240000000, 1.0, QStringLiteral("Playing")), false);
        QCOMPARE(MprisPolicy::isPlaybackRound(
                     120000000, 1000000000, 1000000, 121000000000,
                     240000000, 1.0, QStringLiteral("Playing")), false);
        QCOMPARE(MprisPolicy::isPlaybackRound(
                     239500000, 1000000000, 500000, 2000000000,
                     240000000, 1.0, QStringLiteral("Paused")), false);
        // Rate participates in the remaining-time calculation.
        QCOMPARE(MprisPolicy::isPlaybackRound(
                     239000000, 1000000000, 0, 1500000000,
                     240000000, 2.0, QStringLiteral("Playing")), true);
    }

    void sidraServiceIsRecognizedAsApplePlatform()
    {
        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.sidra");
        QCOMPARE(MprisPolicy::platformFor(state), QStringLiteral("apple"));
    }

    void appleMusicWebPlayerIsMusicByDefault()
    {
        // Regression test for defect 1: music.apple.com was not in
        // musicUrlPrefixes, so the pbi non-music URL branch rejected it before
        // the metadata heuristic ever ran and no search was attempted.
        PolicyConfig config;
        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        state.url = QStringLiteral("https://music.apple.com/cn/album/foo/123?i=456");
        QVERIFY(MprisPolicy::isMusic(state, config));
    }

    void uncheckingAppleDisablesSidraAndWebPlayer()
    {
        PolicyConfig config;
        config.enabledPlatforms = {QStringLiteral("netease")};

        MprisState sidra;
        sidra.service = QStringLiteral("org.mpris.MediaPlayer2.sidra");
        sidra.url = QStringLiteral("https://music.apple.com/cn/album/foo/123?i=456");
        QVERIFY(!MprisPolicy::isMusic(sidra, config));

        MprisState pbi;
        pbi.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        pbi.url = QStringLiteral("https://music.apple.com/cn/album/foo/123?i=456");
        QVERIFY(!MprisPolicy::isMusic(pbi, config));
    }

    void uncheckingNeteaseDisablesWebPlayer()
    {
        PolicyConfig config;
        config.enabledPlatforms = {QStringLiteral("apple")};

        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        state.url = QStringLiteral("https://music.163.com/st/webplayer");
        QVERIFY(!MprisPolicy::isMusic(state, config));
    }

    void unknownSourceIgnoresPlatformToggles()
    {
        // D-2: an unlisted platform must keep working the day this ships,
        // regardless of which checkboxes are set.
        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.mpv");
        state.title = QStringLiteral("Some Song");
        state.artists = {QStringLiteral("Some Artist")};
        state.lengthUs = 200000000;

        PolicyConfig allEnabled;
        QVERIFY(MprisPolicy::isMusic(state, allEnabled));

        PolicyConfig noneEnabled;
        noneEnabled.enabledPlatforms = {};
        QVERIFY(MprisPolicy::isMusic(state, noneEnabled));
    }

    void ciderWildcardDoesNotMatchUnrelatedServiceSubstring()
    {
        // "*cider*" would also match a service like ".decider" (any player
        // whose name merely contains the substring "cider"); the dot-anchored
        // "*.cider*" requires "cider" to start a path segment, same as
        // "*.sidra" right next to it.
        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.decider");
        state.title = QStringLiteral("Some Song");
        state.artists = {QStringLiteral("Some Artist")};
        state.lengthUs = 200000000;
        QVERIFY(MprisPolicy::platformFor(state).isEmpty());

        // It's an unknown platform (D-2), so it must keep working regardless
        // of whether "apple" is checked.
        PolicyConfig config;
        config.enabledPlatforms = {QStringLiteral("netease")};
        QVERIFY(MprisPolicy::isMusic(state, config));
    }

    void customUrlPrefixStillMatches()
    {
        PolicyConfig config;
        config.musicUrlPrefixes = {QStringLiteral("https://example-music.test/")};

        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        state.url = QStringLiteral("https://example-music.test/track/1");
        QVERIFY(MprisPolicy::isMusic(state, config));
    }

    void sidraRadioEntryWithoutUrlIsStillApple()
    {
        PolicyConfig config;
        MprisState state;
        state.service = QStringLiteral("org.mpris.MediaPlayer2.sidra");
        // No xesam:url key at all -- radio/classical entries without a catalogId.
        QCOMPARE(MprisPolicy::platformFor(state), QStringLiteral("apple"));
        QVERIFY(MprisPolicy::isMusic(state, config));
    }

    void musicRejectReasonReportsAllFourCauses()
    {
        // blacklist
        {
            const PolicyConfig config{{QStringLiteral("org.mpris.MediaPlayer2.blocked.*")}, {}, true};
            MprisState state;
            state.service = QStringLiteral("org.mpris.MediaPlayer2.blocked.instance1");
            QCOMPARE(MprisPolicy::musicRejectReason(state, config), QStringLiteral("blacklist"));
        }
        // platform-disabled: Sidra is a recognized "apple" service, but the
        // platform is unchecked in settings.
        {
            PolicyConfig config;
            config.enabledPlatforms = {QStringLiteral("netease")};
            MprisState state;
            state.service = QStringLiteral("org.mpris.MediaPlayer2.sidra");
            state.url = QStringLiteral("https://music.apple.com/cn/album/foo/123?i=456");
            QCOMPARE(MprisPolicy::musicRejectReason(state, config), QStringLiteral("platform-disabled"));
        }
        // browser-non-music: plasma-browser-integration on a non-music URL.
        {
            const PolicyConfig config;
            MprisState state;
            state.service = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
            state.url = QStringLiteral("https://www.bilibili.com/video/BV1mkg36zEfX/");
            QCOMPARE(MprisPolicy::musicRejectReason(state, config), QStringLiteral("browser-non-music"));
        }
        // metadata-heuristic: no platform, no URL, and metadata that fails
        // the heuristic (no title/artist/length).
        {
            const PolicyConfig config;
            MprisState state;
            state.service = QStringLiteral("org.mpris.MediaPlayer2.mpv");
            QCOMPARE(MprisPolicy::musicRejectReason(state, config), QStringLiteral("metadata-heuristic"));
        }
        // Real music reports no reason, and isMusic agrees.
        {
            const PolicyConfig config;
            MprisState state;
            state.service = QStringLiteral("org.mpris.MediaPlayer2.mpv");
            state.title = QStringLiteral("Some Song");
            state.artists = {QStringLiteral("Some Artist")};
            state.lengthUs = 200000000;
            QVERIFY(MprisPolicy::musicRejectReason(state, config).isEmpty());
            QVERIFY(MprisPolicy::isMusic(state, config));
        }
        // useMetadataHeuristic == false skips the heuristic entirely: no
        // platform, no URL, and no metadata at all still counts as music.
        {
            PolicyConfig config;
            config.useMetadataHeuristic = false;
            MprisState state;
            state.service = QStringLiteral("org.mpris.MediaPlayer2.mpv");
            QVERIFY(MprisPolicy::musicRejectReason(state, config).isEmpty());
            QVERIFY(MprisPolicy::isMusic(state, config));
        }
    }

    void replaysSidraFixtures_data()
    {
        QTest::addColumn<QString>("fixtureName");
        QTest::newRow("mandarin") << QStringLiteral("sidra-mandarin.json");
        QTest::newRow("idol") << QStringLiteral("sidra-idol.json");
        QTest::newRow("gunjou") << QStringLiteral("sidra-gunjou.json");
    }

    void replaysSidraFixtures()
    {
        QFETCH(QString, fixtureName);
        QFile file(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR "/") + fixtureName);
        QVERIFY2(file.open(QIODevice::ReadOnly), "sidra fixture was not found");
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        const auto recorded = root.value(QStringLiteral("services")).toArray().first().toObject();
        const auto metadata = recorded.value(QStringLiteral("metadata")).toObject();

        MprisState state;
        state.service = recorded.value(QStringLiteral("service")).toString();
        state.trackId = metadata.value(QStringLiteral("mpris:trackid")).toString();
        state.lengthUs = metadata.value(QStringLiteral("mpris:length")).toInteger();
        state.title = metadata.value(QStringLiteral("xesam:title")).toString();
        for (const auto &artist : metadata.value(QStringLiteral("xesam:artist")).toArray()) {
            state.artists.append(artist.toString());
        }
        state.album = metadata.value(QStringLiteral("xesam:album")).toString();
        state.url = metadata.value(QStringLiteral("xesam:url")).toString();

        // §0.2: Sidra's artist array is already clean, unlike pbi's slash-joined string.
        QCOMPARE(state.artists.size(), 1);
        QCOMPARE(MprisPolicy::platformFor(state), QStringLiteral("apple"));

        // §0.2: no kde:mediaSrc / kde:pid, so the fingerprint takes the meta: branch.
        QVERIFY(state.mediaSrc.isEmpty());
        QVERIFY(MprisPolicy::fingerprint(state).startsWith(QStringLiteral("meta:")));

        PolicyConfig config;
        QVERIFY(MprisPolicy::isMusic(state, config));
    }
};

QTEST_GUILESS_MAIN(MprisPolicyTest)
#include "tst_mprispolicy.moc"
