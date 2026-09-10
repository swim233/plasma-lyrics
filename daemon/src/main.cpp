#include "config.h"
#include "controlservice.h"
#include "logging.h"
#include "resolver.h"
#include "snapshot.h"
#include "core/log/logformat.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"
#include "daemon/src/mpris/mprismanager.h"
#include "providers/local/localprovider.h"
#ifdef PLASMA_LYRICS_HAVE_AMLL
#include "providers/amll/amllprovider.h"
#endif
#ifdef PLASMA_LYRICS_HAVE_NETEASE
#include "providers/netease/neteaseprovider.h"
#endif

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDBusConnection>
#include <QDBusError>
#include <QFile>
#include <QLockFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QScopeGuard>
#include <QSocketNotifier>
#include <QTextStream>
#include <cerrno>
#include <cstdio>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <unistd.h>

using namespace PlasmaLyrics;

namespace {

QFile *logFile = nullptr;
QMutex logMutex;

void mirrorMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QByteArray formatted = qFormatLogMessage(type, context, message).toLocal8Bit() + '\n';
    QMutexLocker locker(&logMutex);
    std::fwrite(formatted.constData(), 1, static_cast<size_t>(formatted.size()), stderr);
    std::fflush(stderr);
    if (logFile && logFile->isOpen()) {
        logFile->write(formatted);
        logFile->flush();
    }
}

void stopMirroredLogging()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&logMutex);
    logFile = nullptr;
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
    parser.addPositionalArgument(QStringLiteral("TITLE"), QStringLiteral("Song title used with --explain."));
    parser.addPositionalArgument(QStringLiteral("ARTIST"), QStringLiteral("Artist used with --explain."), QStringLiteral("[ARTIST]"));
    parser.process(application);

    Config config;
    // Set unconditionally, and before the single-instance lock check below,
    // so a second instance's "already running" failure is formatted the same
    // way whether or not file logging ends up installed (DESIGN.md #49).
    qSetMessagePattern(QStringLiteral(
        "[%{time yyyy-MM-dd hh:mm:ss.zzz}] %{type} %{category} %{message}"));
    if (config.debugLoggingEnabled()) {
        QLoggingCategory::setFilterRules(QStringLiteral("plasmalyrics.*.debug=true"));
    }
    static QFile configuredLog;
    bool mirroredLoggingInstalled = false;
    if (config.fileLoggingEnabled()) {
        configuredLog.setFileName(config.logFilePath());
        QDir().mkpath(QFileInfo(configuredLog.fileName()).absolutePath());
        if (configuredLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            logFile = &configuredLog;
            qInstallMessageHandler(mirrorMessage);
            mirroredLoggingInstalled = true;
        } else {
            qCWarning(lcDaemon).noquote() << "cannot open log file:"
                                          << configuredLog.fileName() + QLatin1Char(':')
                                          << configuredLog.errorString();
        }
    }
    const auto loggingGuard = qScopeGuard([mirroredLoggingInstalled] {
        if (mirroredLoggingInstalled) {
            stopMirroredLogging();
        }
    });
#ifdef PLASMA_LYRICS_HAVE_NETEASE
    NeteaseProvider netease(config.neteaseBaseUrl(), config.networkTimeoutMs());
#endif
#ifdef PLASMA_LYRICS_HAVE_AMLL
    AmllProvider amll(config.amllIndexUrl(), config.amllContentBaseUrl(),
                      config.amllTimeoutMs(), {}, config.amllIndexMaxAgeSeconds());
#endif
    LocalProvider local(config.localLyricsDirectory());
    QStringList supportedProviders{local.id()};
#ifdef PLASMA_LYRICS_HAVE_NETEASE
    supportedProviders.append(netease.id());
#endif
#ifdef PLASMA_LYRICS_HAVE_AMLL
    supportedProviders.append(amll.id());
#endif
    QList<Provider *> providers;
    const QStringList enabledProviderOrder = config.enabledProviderOrder();
    for (const auto &providerId : enabledProviderOrder) {
        if (providerId == QStringLiteral("local") && local.isConfigured()) {
            providers.append(&local);
        }
#ifdef PLASMA_LYRICS_HAVE_NETEASE
        if (providerId == QStringLiteral("netease") && netease.isConfigured()) {
            providers.append(&netease);
        }
#endif
#ifdef PLASMA_LYRICS_HAVE_AMLL
        if (providerId == QStringLiteral("amll") && amll.isConfigured()) {
            providers.append(&amll);
        }
#endif
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
#ifdef PLASMA_LYRICS_HAVE_NETEASE
                    if (providerId == QStringLiteral("netease") && netease.isConfigured()) {
                        available.append(providerId);
                    }
#endif
#ifdef PLASMA_LYRICS_HAVE_AMLL
                    if (providerId == QStringLiteral("amll") && amll.isConfigured()) {
                        available.append(providerId);
                    }
#endif
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
        const TrackQuery query{arguments.first(), artists, QString(), 0};
        return explainProviders(application, query, providers,
                                parser.value(QStringLiteral("platform"))
                                    == QStringLiteral("apple"));
    }

    const QString lockDirectory = QFileInfo(SnapshotWriter::defaultPath()).absolutePath();
    QDir().mkpath(lockDirectory);
    QLockFile lock(lockDirectory + QStringLiteral("/daemon.lock"));
    lock.setStaleLockTime(10000);
    if (!lock.tryLock(100)) {
        qCCritical(lcDaemon, "plasma-lyricsd is already running");
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
    MprisManager manager(config.policy());
    ResolvedLyric resolved{.state = QStringLiteral("filtered"),
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

    const auto forceResolve = [&](const MprisState &state, const QString &trigger) {
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
        resolver.resolve(state, {.force = true, .existing = std::move(existing), .trigger = trigger});
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
        "started version=%1 providers=%2 disabled=%3 store=%4 logFile=%5 debug=%6 filterCredits=%7")
        .arg(QStringLiteral(PLASMA_LYRICS_VERSION),
             enabledProviders.join(QLatin1Char(',')),
             disabledProviders.isEmpty() ? QStringLiteral("-")
                                         : disabledProviders.join(QLatin1Char(',')),
             quoted(store.path()),
             mirroredLoggingInstalled ? quoted(configuredLog.fileName())
                                      : QStringLiteral("none"),
             lcDaemon().isDebugEnabled() ? QStringLiteral("true") : QStringLiteral("false"),
             config.filterCredits() ? QStringLiteral("true") : QStringLiteral("false"));

    update(true);
    return application.exec();
}
