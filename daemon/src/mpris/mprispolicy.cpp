#include "mprispolicy.h"

#include "core/match/matcher.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace PlasmaLyrics {
namespace {

struct PlatformRule {
    QString id;
    QStringList serviceWildcards;
    QStringList urlPrefixes;
};

const QList<PlatformRule> &platformRules()
{
    static const QList<PlatformRule> rules{
        {QStringLiteral("netease"),
         {},
         {QStringLiteral("https://music.163.com/"), QStringLiteral("http://music.163.com/")}},
        {QStringLiteral("apple"),
         {QStringLiteral("*.sidra"), QStringLiteral("*.cider*")},
         {QStringLiteral("https://music.apple.com/"), QStringLiteral("http://music.apple.com/"),
          QStringLiteral("https://classical.music.apple.com/"), QStringLiteral("http://classical.music.apple.com/")}},
    };
    return rules;
}

bool matchesWildcard(const QString &value, const QString &pattern)
{
    const QRegularExpression expression(
        QRegularExpression::wildcardToRegularExpression(pattern),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(value).hasMatch();
}

} // namespace

QString MprisPolicy::platformFor(const MprisState &state)
{
    // Service name first: Sidra's radio/classical entries carry no xesam:url at
    // all, so the service name is the only signal that can identify them.
    for (const auto &rule : platformRules()) {
        for (const auto &pattern : rule.serviceWildcards) {
            if (matchesWildcard(state.service, pattern)) {
                return rule.id;
            }
        }
    }
    if (!state.url.isEmpty()) {
        for (const auto &rule : platformRules()) {
            for (const auto &prefix : rule.urlPrefixes) {
                if (state.url.startsWith(prefix, Qt::CaseInsensitive)) {
                    return rule.id;
                }
            }
        }
    }
    return QString();
}

QString MprisPolicy::fingerprint(const MprisState &state)
{
    if (!state.mediaSrc.isEmpty()) {
        const auto path = QUrl(state.mediaSrc).path();
        QString token = QFileInfo(path).completeBaseName();
        if (token.isEmpty()) {
            token = path.section(QLatin1Char('/'), -1);
        }
        if (!token.isEmpty()) {
            return QStringLiteral("mediaSrc:") + token;
        }
    }
    const QString tuple = normalizeSearchText(state.title) + QLatin1Char('|')
        + cleanArtists(state.artists).join(QLatin1Char('/')) + QLatin1Char('|')
        + normalizeSearchText(state.album) + QLatin1Char('|') + QString::number(state.lengthUs);
    return QStringLiteral("meta:")
        + QString::fromLatin1(QCryptographicHash::hash(tuple.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool MprisPolicy::isBlacklisted(const QString &service, const PolicyConfig &config)
{
    for (const auto &pattern : config.serviceBlacklist) {
        const QRegularExpression expression(
            QRegularExpression::wildcardToRegularExpression(pattern),
            QRegularExpression::CaseInsensitiveOption);
        if (expression.match(service).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool MprisPolicy::isMusic(const MprisState &state, const PolicyConfig &config)
{
    if (isBlacklisted(state.service, config)) {
        return false;
    }
    const QString platform = platformFor(state);
    if (!platform.isEmpty()) {
        // A known platform is decided entirely by whether it's checked in
        // settings; an unlisted platform (D-2) falls through to the untouched
        // heuristics below rather than becoming unreachable.
        return config.enabledPlatforms.contains(platform);
    }
    if (!state.url.isEmpty()) {
        for (const auto &prefix : config.musicUrlPrefixes) {
            if (state.url.startsWith(prefix, Qt::CaseInsensitive)) {
                return true;
            }
        }
        if (state.service.contains(QStringLiteral("plasma-browser-integration"))) {
            return false;
        }
    }
    if (!config.useMetadataHeuristic) {
        return true;
    }
    const auto artists = cleanArtists(state.artists);
    return !normalizeSearchText(state.title).isEmpty()
        && normalizeSearchText(state.title) != QStringLiteral("网易云音乐")
        && !artists.isEmpty()
        && !(artists.size() == 1 && artists.first().isEmpty())
        && state.lengthUs > 0;
}

QString MprisPolicy::choosePlayer(const QList<MprisState> &players,
                                  const QString &currentService,
                                  const PolicyConfig &config)
{
    QList<MprisState> eligible;
    bool hasPbi = false;
    for (auto state : players) {
        state.music = isMusic(state, config);
        if (state.music) {
            eligible.append(state);
            hasPbi = hasPbi || state.service.contains(QStringLiteral("plasma-browser-integration"));
        }
    }
    if (hasPbi) {
        QList<qint64> proxiedPids;
        for (const auto &state : eligible) {
            if (state.service.contains(QStringLiteral("plasma-browser-integration")) && state.kdePid > 0) {
                proxiedPids.append(state.kdePid);
            }
        }
        eligible.erase(std::remove_if(eligible.begin(), eligible.end(), [&proxiedPids](const auto &state) {
            const bool browserOwned =
                state.service.contains(QStringLiteral("chromium"), Qt::CaseInsensitive)
                || state.service.contains(QStringLiteral("chrome"), Qt::CaseInsensitive);
            if (!browserOwned) {
                return false;
            }
            // kde:pid is the integration host's getppid(), which has only ever been
            // observed to be the browser's own pid. Drop just the service that pid
            // owns, so a second browser playing without the extension stays
            // eligible. With no usable pid, fall back to trusting the integration
            // alone rather than showing the same track twice -- DESIGN.md 10.
            if (proxiedPids.isEmpty()) {
                return true;
            }
            return std::any_of(proxiedPids.cbegin(), proxiedPids.cend(), [&state](qint64 pid) {
                return state.service.endsWith(QStringLiteral(".instance") + QString::number(pid));
            });
        }), eligible.end());
    }
    const auto current = std::find_if(eligible.cbegin(), eligible.cend(), [&currentService](const auto &state) {
        return state.service == currentService;
    });
    const auto newestPlaying = std::max_element(eligible.cbegin(), eligible.cend(), [](const auto &left, const auto &right) {
        const bool leftPlaying = left.playbackStatus == QStringLiteral("Playing");
        const bool rightPlaying = right.playbackStatus == QStringLiteral("Playing");
        if (leftPlaying != rightPlaying) {
            return !leftPlaying;
        }
        return left.playingSerial < right.playingSerial;
    });
    if (newestPlaying != eligible.cend() && newestPlaying->playbackStatus == QStringLiteral("Playing")) {
        if (current == eligible.cend() || current->playbackStatus != QStringLiteral("Playing")
            || newestPlaying->playingSerial > current->playingSerial) {
            return newestPlaying->service;
        }
    }
    if (current == eligible.cend() && !eligible.isEmpty()) {
        const auto newest = std::max_element(eligible.cbegin(), eligible.cend(), [](const auto &left, const auto &right) {
            return left.playingSerial < right.playingSerial;
        });
        return newest->service;
    }
    return current != eligible.cend() ? current->service : QString();
}

bool MprisPolicy::isPositionJump(qint64 previousPositionUs,
                                 qint64 previousMonotonicNs,
                                 qint64 positionUs,
                                 qint64 monotonicNs,
                                 double rate,
                                 const QString &status)
{
    if (previousMonotonicNs <= 0 || status != QStringLiteral("Playing")) {
        return false;
    }
    const qint64 expected = previousPositionUs
        + static_cast<qint64>((monotonicNs - previousMonotonicNs) / 1000.0 * rate);
    const qint64 deviation = positionUs - expected;
    return positionUs < previousPositionUs - 3000000 || qAbs(deviation) > 2000000;
}

bool MprisPolicy::isPlaybackRound(qint64 previousPositionUs,
                                  qint64 previousMonotonicNs,
                                  qint64 positionUs,
                                  qint64 monotonicNs,
                                  qint64 lengthUs,
                                  double rate,
                                  const QString &status)
{
    if (lengthUs <= 0 || previousPositionUs < 0 || positionUs < 0
        || previousPositionUs > lengthUs || positionUs > lengthUs
        || previousMonotonicNs <= 0 || monotonicNs <= previousMonotonicNs
        || !std::isfinite(rate) || rate <= 0.0
        || status != QStringLiteral("Playing")) {
        return false;
    }
    const qint64 nearEndWindow = std::min<qint64>(15000000, lengthUs / 5);
    const qint64 nearStartWindow = std::min<qint64>(10000000, lengthUs / 10);
    if (previousPositionUs < lengthUs - nearEndWindow
        || positionUs > nearStartWindow
        || previousPositionUs - positionUs <= 3000000) {
        return false;
    }

    // A real loop can only happen after monotonic playback at the current Rate
    // has consumed the previous sample's remaining duration. This is the
    // evidence that a simple "near end -> near start" test lacks. Keep a small
    // crossing allowance for timestamp/reporting granularity, then require the
    // observed opening position to agree with the expected wrapped phase.
    const long double playedUs = static_cast<long double>(monotonicNs - previousMonotonicNs)
        / 1000.0L * static_cast<long double>(rate);
    if (!std::isfinite(playedUs)) {
        return false;
    }
    const qint64 crossingAllowanceUs = std::min<qint64>(250000, lengthUs / 100);
    const long double remainingUs = static_cast<long double>(lengthUs - previousPositionUs);
    if (playedUs + crossingAllowanceUs < remainingUs) {
        return false;
    }

    const long double expectedPhase = std::fmod(
        static_cast<long double>(previousPositionUs) + playedUs,
        static_cast<long double>(lengthUs));
    const long double directDistance = std::fabs(
        static_cast<long double>(positionUs) - expectedPhase);
    const long double circularDistance = std::min(
        directDistance, static_cast<long double>(lengthUs) - directDistance);
    const qint64 phaseToleranceUs = std::clamp<qint64>(lengthUs / 20, 250000, 2000000);
    return circularDistance <= static_cast<long double>(phaseToleranceUs);
}

} // namespace PlasmaLyrics
