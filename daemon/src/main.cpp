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
    QFile configuredLog;
    if (config.fileLoggingEnabled()) {
        configuredLog.setFileName(config.logFilePath());
        QDir().mkpath(QFileInfo(configuredLog.fileName()).absolutePath());
        if (configuredLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            logFile = &configuredLog;
            qSetMessagePattern(QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz}] %{type} %{message}"));
            qInstallMessageHandler(mirrorMessage);
        }
    }
#ifdef PLASMA_LYRICS_HAVE_NETEASE
    NeteaseProvider netease(config.neteaseBaseUrl(), config.networkTimeoutMs());
#endif
    if (parser.isSet(QStringLiteral("explain"))) {
        const auto arguments = parser.positionalArguments();
        if (arguments.isEmpty()) {
            parser.showHelp(1);
        }
        const TrackQuery query{arguments.first(),
                               {arguments.value(1)}, QString(), 0};
#ifdef PLASMA_LYRICS_HAVE_NETEASE
        const auto candidates = netease.search(query);
        QTextStream(stdout) << explainMatch(query, candidates);
        if (candidates.isEmpty() && !netease.lastError().isEmpty()) {
            QTextStream(stderr) << netease.lastError() << Qt::endl;
        }
        return candidates.isEmpty() ? 1 : 0;
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
            publish(state, searching);
            resolved = resolver.resolve(*state);
        }
        if (resolved.ref) {
            resolved.document.offsetMs = store.offset(*resolved.ref);
        }
        publish(state, resolved);
    };
    QObject::connect(&manager, &MprisManager::activeStateChanged, &application, update);
    update(true);
    return application.exec();
}
