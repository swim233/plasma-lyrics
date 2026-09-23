#include "config.h"
#include "controlservice.h"
#include "logging.h"
#include "logmirrorformat.h"
#include "resolver.h"
#include "snapshot.h"
#include "core/config/proxyspec.h"
#include "core/log/logformat.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"
#include "daemon/src/mpris/mprismanager.h"
#include "providers/local/localprovider.h"
#include "providers/amll/amllprovider.h"
#include "providers/netease/neteaseprovider.h"
#include "providers/qq/qqprovider.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDBusConnection>
#include <QDBusError>
#include <QFile>
#include <QLockFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QScopeGuard>
#include <QSocketNotifier>
#include <QTextStream>
#include <cerrno>
#include <cstdio>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

using namespace PlasmaLyrics;

namespace {

QFile *logFile = nullptr;
QMutex logMutex;
StderrSink stderrSink = StderrSink::Plain;

// journald sets $JOURNAL_STREAM to "device:inode" for the stream socket it
// handed this process as stdout/stderr (sd-daemon(3)). Checking only that
// the variable is set is not enough -- stderr can still have been
// redirected to a file or pipe after the service started, or under a
// manual run -- so the fd actually in use is compared against it via
// fstat(2) rather than trusted on its own.
StderrSink detectStderrSink()
{
    if (::isatty(STDERR_FILENO)) {
        return StderrSink::Tty;
    }
    const QByteArray journalStream = qgetenv("JOURNAL_STREAM");
    if (journalStream.isEmpty()) {
        return StderrSink::Plain;
    }
    struct stat errStat {};
    if (::fstat(STDERR_FILENO, &errStat) != 0) {
        return StderrSink::Plain;
    }
    const QByteArray actual = QByteArray::number(static_cast<qulonglong>(errStat.st_dev))
        + ':' + QByteArray::number(static_cast<qulonglong>(errStat.st_ino));
    return journalStream == actual ? StderrSink::Journald : StderrSink::Plain;
}

void mirrorMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString category = displayCategory(context.category);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));

    // QT_MESSAGE_PATTERN is Qt's own escape hatch for the exact rendered
    // text: Qt reads it once, and when it is set, the qSetMessagePattern()
    // call in main() becomes a no-op -- qFormatLogMessage() below already
    // renders with the env-supplied pattern regardless of that call. Once
    // set, its rendering replaces every hand-rolled body
    // renderMirroredLine() would otherwise build, verbatim -- category
    // stripping included, since %{category} there means the real
    // QLoggingCategory name, same as it always has. The journald "<N>"
    // prefix is still layered on separately in that case: journald cannot
    // parse pattern text, only that leading marker.
    const bool honorEnvPattern = !qEnvironmentVariableIsEmpty("QT_MESSAGE_PATTERN");
    const QString patternedBody = honorEnvPattern ? qFormatLogMessage(type, context, message) : QString();

    const QString stderrLine = renderMirroredLine(stderrSink, type, category, message, timestamp,
                                                   honorEnvPattern, patternedBody);
    const QByteArray formattedStderr = stderrLine.toLocal8Bit() + '\n';
    QMutexLocker locker(&logMutex);
    std::fwrite(formattedStderr.constData(), 1, static_cast<size_t>(formattedStderr.size()), stderr);
    std::fflush(stderr);
    if (logFile && logFile->isOpen()) {
        const QString fileLine = renderMirroredLine(StderrSink::Plain, type, category, message, timestamp,
                                                     honorEnvPattern, patternedBody);
        logFile->write(fileLine.toLocal8Bit() + '\n');
        logFile->flush();
    }
}

void stopMirroredLogging()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&logMutex);
    logFile = nullptr;
}

QString proxyErrorName(ProxySpec::Error error)
{
    switch (error) {
    case ProxySpec::Error::None: return QStringLiteral("None");
    case ProxySpec::Error::InvalidUrl: return QStringLiteral("InvalidUrl");
    case ProxySpec::Error::UnsupportedScheme: return QStringLiteral("UnsupportedScheme");
    case ProxySpec::Error::MissingHost: return QStringLiteral("MissingHost");
    case ProxySpec::Error::MissingPort: return QStringLiteral("MissingPort");
    case ProxySpec::Error::UnexpectedPath: return QStringLiteral("UnexpectedPath");
    }
    return QStringLiteral("Unknown");
}

// Classic self-pipe trick: the signal handler only writes one byte (the only
// async-signal-safe thing it may do) and QSocketNotifier delivers the actual
// shutdown on the Qt event loop thread. One pipe per signal keeps the two
// causes distinguishable without decoding a payload in the handler.
int termPipe[2] = {-1, -1};
int intPipe[2] = {-1, -1};

void writeWakeByte(int fd)
{
    // A signal handler must not leave errno different from how it found it:
    // the interrupted code may be about to check errno itself.
    const int savedErrno = errno;
    const char byte = 1;
    // The write is best-effort by construction: a full pipe still means a
    // wake-up byte is already queued, so a dropped write here cannot lose
    // the shutdown request. Capturing the result only silences
    // -Werror=unused-result under _FORTIFY_SOURCE (Arch's makepkg default).
    [[maybe_unused]] const ssize_t written = ::write(fd, &byte, sizeof(byte));
    errno = savedErrno;
}

void handleSigTerm(int)
{
    writeWakeByte(termPipe[1]);
}

void handleSigInt(int)
{
    writeWakeByte(intPipe[1]);
}

bool installTerminationHandlers(QCoreApplication &application)
{
    if (::pipe2(termPipe, O_NONBLOCK | O_CLOEXEC) != 0
        || ::pipe2(intPipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        return false;
    }

    // Notifiers are wired up before either sigaction() call, and both are in
    // place before the first one is installed. That way a signal delivered
    // between the two sigaction() calls -- or the second one failing outright
    // -- can never land on an installed handler with nobody reading its
    // pipe, which would silently swallow the wake-up byte.
    auto *termNotifier = new QSocketNotifier(termPipe[0], QSocketNotifier::Read, &application);
    QObject::connect(termNotifier, &QSocketNotifier::activated, &application, [] {
        char discard[16];
        while (::read(termPipe[0], discard, sizeof(discard)) > 0) {}
        qCInfo(lcDaemon) << "stopping reason=SIGTERM";
        QCoreApplication::quit();
    });

    auto *intNotifier = new QSocketNotifier(intPipe[0], QSocketNotifier::Read, &application);
    QObject::connect(intNotifier, &QSocketNotifier::activated, &application, [] {
        char discard[16];
        while (::read(intPipe[0], discard, sizeof(discard)) > 0) {}
        qCInfo(lcDaemon) << "stopping reason=SIGINT";
        QCoreApplication::quit();
    });

    struct sigaction termAction = {};
    termAction.sa_handler = handleSigTerm;
    sigemptyset(&termAction.sa_mask);
    termAction.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &termAction, nullptr) != 0) return false;

    struct sigaction intAction = {};
    intAction.sa_handler = handleSigInt;
    sigemptyset(&intAction.sa_mask);
    intAction.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &intAction, nullptr) != 0) return false;

    return true;
}

int explainProviders(QCoreApplication &application, const TrackQuery &query,
                     QList<Provider *> providers, bool allowLocalizedFallback)
{
    QTextStream output(stdout);
    QTextStream errors(stderr);
    QStringList providerIds;
    for (const auto *provider : providers) providerIds.append(provider->id());
    output << "provider chain: " << providerIds.join(QStringLiteral(" -> ")) << Qt::endl;

    struct ExplainState {
        qsizetype index = 0;
        bool complete = false;
        int exitCode = 1;
    };
    const auto state = std::make_shared<ExplainState>();
    const auto next = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakNext = next;
    *next = [&, state, weakNext] {
        if (state->index >= providers.size()) {
            state->complete = true;
            state->exitCode = 1;
            application.exit(1);
            return;
        }
        Provider *provider = providers[state->index++];
        output << "provider: " << provider->id() << Qt::endl
               << "match policy: "
               << (provider->matchPolicy() == MatchPolicy::PreserveVersions
                       ? QStringLiteral("preserve-versions")
                       : QStringLiteral("default"))
               << Qt::endl;
        provider->search(query, [&, state, weakNext, provider](ProviderSearchResult result) {
            if (!result.error.isEmpty()) {
                errors << "provider error [" << provider->id() << "]: "
                       << result.error << Qt::endl;
                if (const auto continuation = weakNext.lock()) (*continuation)();
                return;
            }
            output << explainMatch(query, result.candidates, allowLocalizedFallback,
                                   true, provider->matchPolicy());
            const auto chosen = chooseMatch(
                rankCandidates(query, result.candidates, provider->matchPolicy()),
                allowLocalizedFallback);
            if (!chosen) {
                if (const auto continuation = weakNext.lock()) (*continuation)();
                return;
            }
            state->complete = true;
            state->exitCode = 0;
            application.exit(0);
        });
    };
    (*next)();
    const int exitCode = state->complete ? state->exitCode : application.exec();
    // Break the last owner -> captures edge deterministically. The callback
    // only keeps a weak continuation, so there is no self-owning cycle when
    // --explain finishes synchronously or asynchronously.
    *next = {};
    return exitCode;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("plasma-lyricsd"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLASMA_LYRICS_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Plasma desktop lyrics service"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("explain"), QStringLiteral("Search and explain candidate scoring for TITLE and ARTIST.")});
    parser.addOption({QStringLiteral("provider"),
                      QStringLiteral("Restrict --explain to one configured lyrics provider."),
                      QStringLiteral("provider")});
    parser.addOption({QStringLiteral("platform"),
                      QStringLiteral("Playback platform used by --explain (for example apple)."),
                      QStringLiteral("platform")});
    // Named, not a third positional: ARTIST is already optional, so a third
    // positional would be ambiguous about which argument was omitted.
    // Without this, --explain always had TrackQuery::lengthMs == 0
    // (durationComparable permanently false, unlike a real MPRIS-driven
    // resolve), so it could not reproduce a duration-dependent decision --
    // the gloss-variant gate under MatchPolicy::Default, or decision 8's
    // localized fallback (which requires deltaMs <= 250) under
    // MatchPolicy::PreserveVersions -- either of which --explain otherwise
    // always disagrees with the daemon about.
    parser.addOption({QStringLiteral("length-ms"),
                      QStringLiteral("Track length in milliseconds used with --explain, "
                                    "for duration-dependent scoring decisions."),
                      QStringLiteral("length-ms")});
    parser.addPositionalArgument(QStringLiteral("TITLE"), QStringLiteral("Song title used with --explain."));
    parser.addPositionalArgument(QStringLiteral("ARTIST"), QStringLiteral("Artist used with --explain."), QStringLiteral("[ARTIST]"));
    parser.process(application);

    Config config;
    // Set unconditionally, and before the single-instance lock check below
    // (DESIGN.md #49's ordering). mirrorMessage is installed unconditionally
    // too and does its own per-sink formatting on the normal path, so this
    // pattern no longer drives normal output. What still reads it: Qt's
    // default handler when stderr is a tty, and on Qt builds without a
    // journald-aware default handler -- on this project's own Arch build,
    // Qt links libsystemd and its default handler calls sd_journal_send()
    // directly when stderr is not a tty (unless QT_FORCE_STDERR_LOGGING or
    // QT_LOGGING_TO_CONSOLE forces the stderr path instead), bypassing this
    // pattern entirely. So in the daemon's actual deployment (journald,
    // non-tty, neither of those set), this call only matters for the
    // narrow shutdown window after loggingGuard
    // uninstalls mirrorMessage -- and even then it is inert whenever
    // QT_MESSAGE_PATTERN is set: Qt reads that env var once and from then
    // on treats qSetMessagePattern() as a no-op, not something the env var
    // "overrides" after the fact.
    qSetMessagePattern(QStringLiteral(
        "[%{time yyyy-MM-dd hh:mm:ss.zzz}] %{type} %{category} %{message}"));
    stderrSink = detectStderrSink();
    qInstallMessageHandler(mirrorMessage);
    if (config.debugLoggingEnabled()) {
        QLoggingCategory::setFilterRules(QStringLiteral("plasmalyrics.*.debug=true"));
    }
    static QFile configuredLog;
    bool fileMirrorActive = false;
    if (config.fileLoggingEnabled()) {
        configuredLog.setFileName(config.logFilePath());
        QDir().mkpath(QFileInfo(configuredLog.fileName()).absolutePath());
        if (configuredLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            logFile = &configuredLog;
            fileMirrorActive = true;
        } else {
            qCWarning(lcDaemon).noquote() << "cannot open log file:"
                                          << configuredLog.fileName() + QLatin1Char(':')
                                          << configuredLog.errorString();
        }
    }
    const auto loggingGuard = qScopeGuard(stopMirroredLogging);

    // Proxy is applied before any provider is constructed and before the
    // --explain branch, so every network request either source makes --
    // command line included -- goes through it. There is deliberately no
    // loopback exemption (DESIGN.md decision 63).
    const QString proxyMode = config.proxyMode();
    QString proxySummary;
    bool proxyBlocksNetworkProviders = false;
    if (proxyMode == QStringLiteral("system")) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        proxySummary = QStringLiteral("system");
    } else if (proxyMode == QStringLiteral("manual")) {
        ProxySpec::Error proxyError = ProxySpec::Error::None;
        const auto proxySpec = ProxySpec::parse(config.proxyUrl(), &proxyError);
        if (proxySpec) {
            QNetworkProxy::setApplicationProxy(QNetworkProxy(
                proxySpec->type() == ProxySpec::Type::Socks5 ? QNetworkProxy::Socks5Proxy
                                                              : QNetworkProxy::HttpProxy,
                proxySpec->host(), proxySpec->port(), proxySpec->user(), proxySpec->password()));
            proxySummary = proxySpec->display();
        } else {
            // Fail-closed: an unusable manual address must not silently fall
            // back to a direct connection for the network providers.
            proxyBlocksNetworkProviders = true;
            proxySummary = QStringLiteral("invalid");
            // redactedForLog() only ever rebuilds scheme/host/port from what
            // QUrl recognized as the authority; it never copies the input's
            // path, query, fragment or userinfo, so nothing else in the
            // configured string -- credentials included, wherever they
            // ended up -- can reach this log line. An empty result means it
            // could not find a safe-enough authority to show at all (most
            // commonly a missing "//"), so only the length is logged then.
            const QString redacted = ProxySpec::redactedForLog(config.proxyUrl());
            if (redacted.isEmpty()) {
                qCWarning(lcDaemon).noquote() << QStringLiteral(
                    "proxy configuration rejected: error=%1 length=%2")
                    .arg(proxyErrorName(proxyError))
                    .arg(config.proxyUrl().trimmed().length());
            } else {
                qCWarning(lcDaemon).noquote() << QStringLiteral(
                    "proxy configuration rejected: error=%1 url=%2")
                    .arg(proxyErrorName(proxyError), quoted(redacted));
            }
        }
    } else {
        proxySummary = QStringLiteral("none");
    }

    NeteaseProvider netease(config.neteaseBaseUrl(), config.networkTimeoutMs());
    AmllProvider amll(config.amllIndexUrl(), config.amllContentBaseUrl(),
                      config.amllTimeoutMs(), {}, config.amllIndexMaxAgeSeconds());
    QqProvider qq(config.qqSearchBaseUrl(), config.qqLyricBaseUrl(), config.qqTimeoutMs());
    LocalProvider local(config.localLyricsDirectory());
    QStringList supportedProviders{local.id()};
    supportedProviders.append(netease.id());
    supportedProviders.append(amll.id());
    supportedProviders.append(qq.id());
    QList<Provider *> providers;
    QStringList enabledProviderOrder = config.enabledProviderOrder();
    if (proxyBlocksNetworkProviders) {
        // Equivalent to the user having left netease/amll unchecked in
        // providers/enabled: the local provider is never affected.
        enabledProviderOrder.removeAll(QStringLiteral("netease"));
        enabledProviderOrder.removeAll(QStringLiteral("amll"));
        enabledProviderOrder.removeAll(QStringLiteral("qq"));
    }
    for (const auto &providerId : enabledProviderOrder) {
        if (providerId == QStringLiteral("local") && local.isConfigured()) {
            providers.append(&local);
        }
        if (providerId == QStringLiteral("netease") && netease.isConfigured()) {
            providers.append(&netease);
        }
        if (providerId == QStringLiteral("amll") && amll.isConfigured()) {
            providers.append(&amll);
        }
        if (providerId == QStringLiteral("qq") && qq.isConfigured()) {
            providers.append(&qq);
        }
    }
    if (providers.isEmpty()) {
        // Invalid URLs or a hand-edited enabled list must not leave the
        // resolver without a source. Local is always safe: it can still find
        // an adjacent sidecar even when its search directory is empty.
        qCWarning(lcDaemon) << "no configured provider can be assembled; using the local provider";
        providers.append(&local);
    }
    if (parser.isSet(QStringLiteral("explain"))) {
        const auto arguments = parser.positionalArguments();
        if (arguments.isEmpty()) {
            QTextStream(stderr) << parser.helpText();
            return 1;
        }
        const QString requestedProvider = parser.value(QStringLiteral("provider"));
        if (!requestedProvider.isEmpty()) {
            providers.erase(std::remove_if(
                providers.begin(), providers.end(),
                [&requestedProvider](const Provider *provider) {
                    return provider->id() != requestedProvider;
                }), providers.end());
            if (providers.isEmpty()) {
                QStringList available;
                for (const auto &providerId : enabledProviderOrder) {
                    if (providerId == QStringLiteral("local") && local.isConfigured()) {
                        available.append(providerId);
                    }
                    if (providerId == QStringLiteral("netease") && netease.isConfigured()) {
                        available.append(providerId);
                    }
                    if (providerId == QStringLiteral("amll") && amll.isConfigured()) {
                        available.append(providerId);
                    }
                    if (providerId == QStringLiteral("qq") && qq.isConfigured()) {
                        available.append(providerId);
                    }
                }
                QTextStream(stderr)
                    << "requested provider is unavailable: " << requestedProvider << Qt::endl
                    << "available providers: "
                    << (available.isEmpty() ? QStringLiteral("none")
                                            : available.join(QStringLiteral(" -> ")))
                    << Qt::endl;
                return 1;
            }
        }
        if (providers.isEmpty()) {
            QTextStream(stderr) << "available providers: none" << Qt::endl;
            return 1;
        }
        QStringList artists;
        if (!arguments.value(1).isEmpty()) artists.append(arguments.value(1));
        qint64 explainLengthMs = 0;
        if (parser.isSet(QStringLiteral("length-ms"))) {
            bool lengthOk = false;
            const qint64 parsedLength = parser.value(QStringLiteral("length-ms")).toLongLong(&lengthOk);
            if (!lengthOk || parsedLength <= 0) {
                QTextStream(stderr) << "invalid --length-ms value: "
                                    << parser.value(QStringLiteral("length-ms")) << Qt::endl;
                return 1;
            }
            explainLengthMs = parsedLength;
        }
        const TrackQuery query{.title = arguments.first(),
                               .artists = artists,
                               .album = QString(),
                               .lengthMs = explainLengthMs,
                               .platformIds = {},
                               .mediaSrc = QString()};
        return explainProviders(application, query, providers,
                                parser.value(QStringLiteral("platform"))
                                    == QStringLiteral("apple"));
    }

    const QString lockDirectory = QFileInfo(SnapshotWriter::defaultPath()).absolutePath();
    QDir().mkpath(lockDirectory);
    QLockFile lock(lockDirectory + QStringLiteral("/daemon.lock"));
    lock.setStaleLockTime(10000);
    if (!lock.tryLock(100)) {
        // Another instance already holding the lock is a benign startup
        // race, not a fault -- warning, not critical, so it doesn't show up
        // in `journalctl -p 3` alongside real problems. The exit code (2)
        // is unrelated and unchanged: systemd's Restart=on-failure keys off
        // that, not the log priority.
        qCWarning(lcDaemon, "plasma-lyricsd is already running");
        return 2;
    }

    // Installed as early as possible in the daemon-only path (--explain
    // already returned above) so a signal arriving during store/resolver/
    // D-Bus setup still produces a "stopping" line and a clean exit instead
    // of the process dying under the default or inherited disposition.
    if (!installTerminationHandlers(application)) {
        qCWarning(lcDaemon) << "cannot install SIGTERM/SIGINT handlers; graceful shutdown disabled";
    }

    LyricStore store;
    QString error;
    if (!store.open(&error)) {
        qCCritical(lcDaemon).noquote() << "cannot open lyric store:" << error;
        return 3;
    }
    Resolver resolver(store, providers, config.filterCredits());
    SnapshotWriter snapshots;
    // One-time, idempotent: cleans up a leftover `@Invalid()` literal on
    // players/blacklist, filter/musicUrlPrefixes and filter/platforms
    // before the config.policy() call immediately below, its only consumer
    // in this entire binary (see DESIGN.md decision 67 -- not one
    // justification, and NOT a severity comparison across the three keys;
    // an earlier version of this reasoning tried that and got the
    // severity ranking backwards). players/blacklist turns back into
    // "unset" on case-specific evidence alone (a real journal capture plus
    // the user's own request to have kdeconnect filtering restored) --
    // that evidence covers only this one key. filter/musicUrlPrefixes also
    // turns back into "unset", but the choice is unobservable rather than
    // evidence-backed: the built-in default here is byte-identical to
    // platformRules()'s netease urlPrefixes, and
    // MprisPolicy::musicRejectReason() decides via platformFor() before it
    // ever reaches the musicUrlPrefixes loop, so "unset" and "explicitly
    // empty" produce identical results for every URL the default would
    // have matched -- it migrates the same way as players/blacklist
    // because Q24 named the two together, not on its own argument.
    // filter/platforms turns into "explicitly empty" instead, on origin
    // rather than severity: `@Invalid()` here can only come from a user
    // actively unchecking both platform checkboxes, so it is real state,
    // not a defect artifact. Severity actually points the other way --
    // musicRejectReason() rejects every enabled-platform track
    // unconditionally once its platform is unchecked, more severe than
    // musicUrlPrefixes above -- so it is not the basis for this one.
    // Placed here, not earlier, for two independent reasons: (1) this call
    // can write the settings file (via an unconditional sync()), and
    // --explain -- a read-only diagnostic a user runs by hand, repeatedly,
    // possibly while the real daemon is already running -- always returns
    // above and never reaches config.policy() at all, so it must never run
    // this; (2) being the sole gate in front of policy()'s only caller
    // means the single-instance QLockFile above structurally rules out a
    // concurrent migration race between two daemon instances: only the
    // lock's winner ever reaches this line, so there is no second writer
    // to race against, not merely one that would self-heal if it happened.
    // (Placement relative to qInstallMessageHandler(mirrorMessage) above
    // is not a third reason: nothing on this call's path --
    // migrateLegacySettings() or either helper it calls -- logs anything
    // at all, so there is no warning here that could reach the wrong
    // handler.) The race guarantee above is specific to daemon-vs-daemon:
    // BackendConfig::load() (frontend/qmlmodule/backendconfig.cpp) runs the
    // same migration under no lock at all, so a daemon-vs-settings-dialog
    // or dialog-vs-dialog race is still just known-untested-but-idempotent,
    // not eliminated -- see DESIGN.md decision 67.
    config.migrateLegacySettings();
    MprisManager manager(config.policy());
    ResolvedLyric resolved{.state = QStringLiteral("filtered"),
                           .ref = std::nullopt,
                           .document = {},
                           .preferredProvider = QString(),
                           .effectivePreferredProvider = QString(),
                           .availableProviders = {},
                           .switchingProvider = {}};
    QString fingerprint;

    const auto publish = [&snapshots, &store](const std::optional<MprisState> &state,
                                             ResolvedLyric lyric) {
        lyric.globalOffsetEnabled = store.globalOffsetEnabled();
        lyric.document.offsetMs = lyric.globalOffsetEnabled
            ? store.globalOffsetMs()
            : lyric.ref ? store.offset(*lyric.ref) : 0;
        QString error;
        if (!snapshots.write(state, lyric, &error)) {
            qCWarning(lcDaemon).noquote() << "cannot write lyric snapshot:" << error;
        }
    };

    // "startup" describes the daemon's first-ever resolve, not merely the
    // first call to this lambda: if no player is active yet when update(true)
    // runs first, nothing resolves, and the label should still land on
    // whichever activeStateChanged eventually supplies the first track.
    bool firstResolvePending = true;
    QString lastActiveService;
    const auto update = [&] (bool trackChanged) {
        const auto state = manager.activeState();
        if (!state) {
            resolver.cancel();
            fingerprint.clear();
            // A vanished player must not be mistaken for a still-active one
            // when a (possibly different) player reappears next.
            lastActiveService.clear();
            resolved = ResolvedLyric{.state = QStringLiteral("filtered"),
                                     .ref = std::nullopt,
                                     .document = {},
                                     .preferredProvider = QString(),
                                     .effectivePreferredProvider = QString(),
                                     .availableProviders = {},
                                     .switchingProvider = {}};
            publish(std::nullopt, resolved);
            return;
        }
        const bool newTrack = trackChanged || state->fingerprint != fingerprint;
        if (newTrack) {
            fingerprint = state->fingerprint;
            ResolvedLyric searching{
                .state = state->music ? QStringLiteral("searching")
                                      : QStringLiteral("filtered"),
                .ref = std::nullopt,
                .document = {},
                .preferredProvider = QString(),
                .effectivePreferredProvider = QString(),
                .availableProviders = {},
                .switchingProvider = {},
            };
            searching.preferredProvider = store.preferredProvider(fingerprint).value_or(QString());
            searching.availableProviders = resolver.availableProviders();
            searching.effectivePreferredProvider = searching.preferredProvider.isEmpty()
                ? searching.availableProviders.value(0) : searching.preferredProvider;
            resolved = searching;
            publish(state, resolved);
            const QString trigger = firstResolvePending
                ? QStringLiteral("startup")
                : (state->service != lastActiveService ? QStringLiteral("player-changed")
                                                        : QStringLiteral("track-changed"));
            firstResolvePending = false;
            lastActiveService = state->service;
            resolver.resolve(*state, trigger);
            return;
        }
        if (resolved.ref) {
            resolved.document.offsetMs = store.offset(*resolved.ref);
        }
        publish(state, resolved);
    };
    QObject::connect(&resolver, &Resolver::resolved, &application,
                     [&](const QString &resolvedFingerprint, const ResolvedLyric &lyric) {
        const auto state = manager.activeState();
        if (!state || resolvedFingerprint != fingerprint
            || state->fingerprint != resolvedFingerprint) {
            return;
        }
        resolved = lyric;
        if (resolved.ref) {
            resolved.document.offsetMs = store.offset(*resolved.ref);
        }
        publish(state, resolved);
    });
    QObject::connect(&manager, &MprisManager::activeStateChanged, &application, update);
    QObject::connect(&manager, &MprisManager::playbackRoundStarted, &application, [&] {
        const auto state = manager.activeState();
        if (!state || !state->music || state->fingerprint != fingerprint) {
            return;
        }
        // Normal resolve publishes an existing fallback immediately and only
        // retries the preferred provider when its cooldown has expired. The
        // player emits this once for each detected end-to-start wrap, so
        // progress polls and ordinary seeks cannot create a retry loop.
        resolver.resolve(*state, QStringLiteral("replay"));
    });

    const auto forceResolve = [&](const MprisState &state, const QString &trigger,
                                  ControlService::CachePolicy cache) {
        std::optional<ResolvedLyric> existing;
        if (resolved.state == QStringLiteral("ok") && resolved.ref
            && !resolved.document.lines.isEmpty()) {
            existing = resolved;
        } else {
            // A stale searching/error snapshot is not content that can be
            // retained. Publish a fresh in-progress state and let Resolver
            // replace it with not-found/network-error if every source fails.
            resolved.state = state.music ? QStringLiteral("searching")
                                         : QStringLiteral("filtered");
            resolved.ref.reset();
            resolved.document = {};
            resolved.temporaryFallback = false;
        }
        resolved.preferredProvider = store.preferredProvider(state.fingerprint).value_or(QString());
        const auto available = resolver.availableProviders();
        resolved.effectivePreferredProvider = resolved.preferredProvider.isEmpty()
            ? available.value(0) : resolved.preferredProvider;
        resolved.temporaryFallback = resolved.ref
            && !resolved.effectivePreferredProvider.isEmpty()
            && resolved.ref->provider != resolved.effectivePreferredProvider;
        resolved.availableProviders = available;
        resolved.switchingProvider = resolved.effectivePreferredProvider;
        publish(state, resolved);
        resolver.resolve(state, {.force = true,
                                 .preferCache = cache == ControlService::CachePolicy::PreferCached,
                                 .existing = std::move(existing),
                                 .trigger = trigger});
    };
    const auto republishCurrent = [&] {
        const auto state = manager.activeState();
        if (!state) {
            if (fingerprint.isEmpty()) publish(std::nullopt, resolved);
            return;
        }
        if (state->fingerprint != fingerprint) {
            return;
        }
        publish(state, resolved);
    };
    ControlService control(
        store, resolver, [&manager] { return manager.activeState(); }, forceResolve,
        [&resolved] { return resolved.ref; }, republishCurrent,
        supportedProviders);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(ControlService::serviceName())
        || !bus.registerObject(ControlService::objectPath(), &control,
                               QDBusConnection::ExportAllSlots)) {
        qCCritical(lcDaemon).noquote() << QStringLiteral("cannot register control D-Bus interface: ")
                                          + bus.lastError().message();
        return 4;
    }

    const QStringList enabledProviders = resolver.availableProviders();
    QStringList disabledProviders;
    for (const auto &providerId : supportedProviders) {
        if (!enabledProviders.contains(providerId)) {
            disabledProviders.append(providerId);
        }
    }
    qCInfo(lcDaemon).noquote() << QStringLiteral(
        "started version=%1 providers=%2 disabled=%3 store=%4 logFile=%5 debug=%6 filterCredits=%7 proxy=%8")
        .arg(QStringLiteral(PLASMA_LYRICS_VERSION),
             enabledProviders.join(QLatin1Char(',')),
             disabledProviders.isEmpty() ? QStringLiteral("-")
                                         : disabledProviders.join(QLatin1Char(',')),
             quoted(store.path()),
             fileMirrorActive ? quoted(configuredLog.fileName())
                                      : QStringLiteral("none"),
             lcDaemon().isDebugEnabled() ? QStringLiteral("true") : QStringLiteral("false"),
             config.filterCredits() ? QStringLiteral("true") : QStringLiteral("false"),
             proxySummary);

    update(true);
    return application.exec();
}
