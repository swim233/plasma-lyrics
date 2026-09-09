#include "config.h"
#include "controlservice.h"
#include "resolver.h"
#include "snapshot.h"
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
#include <QMutex>
#include <QScopeGuard>
#include <QTextStream>
#include <cstdio>
#include <functional>
#include <memory>

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
    static QFile configuredLog;
    bool mirroredLoggingInstalled = false;
    if (config.fileLoggingEnabled()) {
        configuredLog.setFileName(config.logFilePath());
        QDir().mkpath(QFileInfo(configuredLog.fileName()).absolutePath());
        if (configuredLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            logFile = &configuredLog;
            qSetMessagePattern(QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz}] %{type} %{message}"));
            qInstallMessageHandler(mirrorMessage);
            mirroredLoggingInstalled = true;
        } else {
            qWarning().noquote() << "cannot open log file:"
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
    QList<Provider *> onlineProviders;
    for (const auto &providerId : config.providerOrder()) {
#ifdef PLASMA_LYRICS_HAVE_NETEASE
        if (providerId == QStringLiteral("netease") && netease.isConfigured()) {
            onlineProviders.append(&netease);
        }
#endif
#ifdef PLASMA_LYRICS_HAVE_AMLL
        if (providerId == QStringLiteral("amll") && amll.isConfigured()) {
            onlineProviders.append(&amll);
        }
#endif
    }
    if (parser.isSet(QStringLiteral("explain"))) {
        const auto arguments = parser.positionalArguments();
        if (arguments.isEmpty()) {
            QTextStream(stderr) << parser.helpText();
            return 1;
        }
        const QString requestedProvider = parser.value(QStringLiteral("provider"));
        if (!requestedProvider.isEmpty()) {
            onlineProviders.erase(std::remove_if(
                onlineProviders.begin(), onlineProviders.end(),
                [&requestedProvider](const Provider *provider) {
                    return provider->id() != requestedProvider;
                }), onlineProviders.end());
            if (onlineProviders.isEmpty()) {
                QStringList available;
                for (const auto &providerId : config.providerOrder()) {
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
        if (onlineProviders.isEmpty()) {
            QTextStream(stderr) << "available providers: none" << Qt::endl;
            return 1;
        }
        QStringList artists;
        if (!arguments.value(1).isEmpty()) artists.append(arguments.value(1));
        const TrackQuery query{arguments.first(), artists, QString(), 0};
        return explainProviders(application, query, onlineProviders,
                                parser.value(QStringLiteral("platform"))
                                    == QStringLiteral("apple"));
    }

    const QString lockDirectory = QFileInfo(SnapshotWriter::defaultPath()).absolutePath();
    QDir().mkpath(lockDirectory);
    QLockFile lock(lockDirectory + QStringLiteral("/daemon.lock"));
    lock.setStaleLockTime(10000);
    if (!lock.tryLock(100)) {
        qCritical("plasma-lyricsd is already running");
        return 2;
    }

    LyricStore store;
    QString error;
    if (!store.open(&error)) {
        qCritical().noquote() << "cannot open lyric store:" << error;
        return 3;
    }
    LocalProvider local;
    QList<Provider *> providers{&local};
    providers.append(onlineProviders);
    Resolver resolver(store, providers, config.filterCredits());
    SnapshotWriter snapshots;
    MprisManager manager(config.policy());
    ResolvedLyric resolved{QStringLiteral("filtered"), std::nullopt, {}};
    QString fingerprint;

    const auto publish = [&snapshots](const std::optional<MprisState> &state,
                                      const ResolvedLyric &lyric) {
        QString error;
        if (!snapshots.write(state, lyric, &error)) {
            qWarning().noquote() << "cannot write lyric snapshot:" << error;
        }
    };

    const auto update = [&] (bool trackChanged) {
        const auto state = manager.activeState();
        if (!state) {
            resolver.cancel();
            fingerprint.clear();
            resolved = {QStringLiteral("filtered"), std::nullopt, {}};
            publish(std::nullopt, resolved);
            return;
        }
        const bool newTrack = trackChanged || state->fingerprint != fingerprint;
        if (newTrack) {
            fingerprint = state->fingerprint;
            ResolvedLyric searching{state->music ? QStringLiteral("searching") : QStringLiteral("filtered"),
                                    std::nullopt, {}};
            searching.preferredProvider = store.preferredProvider(fingerprint).value_or(QString());
            searching.availableProviders = resolver.availableProviders();
            searching.effectivePreferredProvider = searching.preferredProvider.isEmpty()
                ? searching.availableProviders.value(0) : searching.preferredProvider;
            resolved = searching;
            publish(state, resolved);
            resolver.resolve(*state);
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
        resolver.resolve(*state);
    });

    const auto forceResolve = [&](const MprisState &state) {
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
        publish(state, resolved);
        resolver.resolve(state, {.force = true, .existing = std::move(existing)});
    };
    ControlService control(store, resolver, [&manager] { return manager.activeState(); },
                           forceResolve);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(ControlService::serviceName())
        || !bus.registerObject(ControlService::objectPath(), &control,
                               QDBusConnection::ExportAllSlots)) {
        qCritical().noquote() << QStringLiteral("cannot register control D-Bus interface: ")
                                 + bus.lastError().message();
        return 4;
    }
    update(true);
    return application.exec();
}
