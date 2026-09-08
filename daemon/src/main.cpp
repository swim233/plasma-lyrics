#include "config.h"
#include "resolver.h"
#include "snapshot.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"
#include "daemon/src/mpris/mprismanager.h"
#include "providers/local/localprovider.h"
#include "providers/netease/neteaseprovider.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMutex>
#include <QScopeGuard>
#include <QTextStream>
#include <cstdio>

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
    if (parser.isSet(QStringLiteral("explain"))) {
        const auto arguments = parser.positionalArguments();
        if (arguments.isEmpty()) {
            QTextStream(stderr) << parser.helpText();
            return 1;
        }
        const TrackQuery query{arguments.first(),
                               {arguments.value(1)}, QString(), 0};
#ifdef PLASMA_LYRICS_HAVE_NETEASE
        netease.search(query, [&application, query](ProviderSearchResult result) {
            if (!result.error.isEmpty()) {
                QTextStream(stderr) << result.error << Qt::endl;
            } else {
                QTextStream(stdout) << explainMatch(query, result.candidates, true, false);
            }
            application.exit(result.candidates.isEmpty() ? 1 : 0);
        });
        return application.exec();
#else
        QTextStream(stderr) << "NetEase provider was disabled at build time." << Qt::endl;
        return 1;
#endif
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
#ifdef PLASMA_LYRICS_HAVE_NETEASE
    providers.append(&netease);
#endif
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
            const ResolvedLyric searching{state->music ? QStringLiteral("searching") : QStringLiteral("filtered"),
                                          std::nullopt, {}};
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
    update(true);
    return application.exec();
}
