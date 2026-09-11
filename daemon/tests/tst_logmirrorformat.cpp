#include "daemon/src/logmirrorformat.h"

#include <QTest>

using namespace PlasmaLyrics;

namespace {
const QString kTimestamp = QStringLiteral("2026-09-11 12:00:00.000");
} // namespace

class LogMirrorFormatTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void stripsPlasmalyricsPrefix()
    {
        QCOMPARE(displayCategory("plasmalyrics.daemon"), QStringLiteral("daemon"));
        QCOMPARE(displayCategory("plasmalyrics.provider.netease"), QStringLiteral("provider.netease"));
        // A category that does not start with the prefix is passed through
        // unchanged rather than mangled -- this should never happen for our
        // own six categories, but the function must not assume it can't.
        QCOMPARE(displayCategory("not.plasmalyrics.daemon"), QStringLiteral("not.plasmalyrics.daemon"));
    }

    void mapsTypeNames()
    {
        QCOMPARE(typeName(QtDebugMsg), QLatin1String("debug"));
        QCOMPARE(typeName(QtInfoMsg), QLatin1String("info"));
        QCOMPARE(typeName(QtWarningMsg), QLatin1String("warning"));
        QCOMPARE(typeName(QtCriticalMsg), QLatin1String("critical"));
        QCOMPARE(typeName(QtFatalMsg), QLatin1String("fatal"));
    }

    // All five QtMsgType values, including debug=7 and fatal=2, which
    // manual/journalctl verification during review never triggered (no
    // debug logging and no crash on demand) -- only warning=4/critical=3/
    // info=6 had been seen live in a journal.
    void mapsSyslogPriorities()
    {
        QCOMPARE(syslogPriority(QtDebugMsg), 7);
        QCOMPARE(syslogPriority(QtInfoMsg), 6);
        QCOMPARE(syslogPriority(QtWarningMsg), 4);
        QCOMPARE(syslogPriority(QtCriticalMsg), 3);
        QCOMPARE(syslogPriority(QtFatalMsg), 2);
    }

    void mapsAnsiColors()
    {
        QCOMPARE(ansiColor(QtDebugMsg), QLatin1String("\x1b[2m"));
        QCOMPARE(ansiColor(QtInfoMsg), QLatin1String(""));
        QCOMPARE(ansiColor(QtWarningMsg), QLatin1String("\x1b[33m"));
        QCOMPARE(ansiColor(QtCriticalMsg), QLatin1String("\x1b[31m"));
        QCOMPARE(ansiColor(QtFatalMsg), QLatin1String("\x1b[1;31m"));
    }

    void plainLineFormatIsLocked()
    {
        // This is the format everyone saw before this feature existed, and
        // it is still what Plain-sink stderr and the log file mirror
        // render by default. Any change here is a user-visible regression.
        QCOMPARE(plainLine(kTimestamp, QtInfoMsg, QStringLiteral("daemon"), QStringLiteral("started")),
                 QStringLiteral("[2026-09-11 12:00:00.000] info daemon started"));
    }

    // --- renderMirroredLine: the 6 (sink x honorEnvPattern) combinations ---
    //
    // Plain is exercised here as both stderr's own Plain sink and as a
    // stand-in for the log-file mirror: mirrorMessage() always calls
    // renderMirroredLine(StderrSink::Plain, ...) for the file regardless of
    // the actual stderr sink, so there is no separate "file" branch at this
    // layer -- these Plain-sink cases are that branch.

    void rendersPlainSinkWithoutEnvPattern()
    {
        QCOMPARE(renderMirroredLine(StderrSink::Plain, QtInfoMsg, QStringLiteral("daemon"),
                                     QStringLiteral("started"), kTimestamp, false, QString()),
                 QStringLiteral("[2026-09-11 12:00:00.000] info daemon started"));
    }

    void rendersPlainSinkWithEnvPattern()
    {
        const QString patterned = QStringLiteral("ENVTEST|info|plasmalyrics.daemon|started");
        QCOMPARE(renderMirroredLine(StderrSink::Plain, QtInfoMsg, QStringLiteral("daemon"),
                                     QStringLiteral("started"), kTimestamp, true, patterned),
                 patterned);
    }

    void rendersTtySinkWithoutEnvPattern()
    {
        // info gets no color at all -- same bytes as Plain.
        QCOMPARE(renderMirroredLine(StderrSink::Tty, QtInfoMsg, QStringLiteral("daemon"),
                                     QStringLiteral("started"), kTimestamp, false, QString()),
                 QStringLiteral("[2026-09-11 12:00:00.000] info daemon started"));
        // Every non-info severity wraps the plain line in its ANSI color
        // and a trailing reset.
        QCOMPARE(renderMirroredLine(StderrSink::Tty, QtWarningMsg, QStringLiteral("daemon"),
                                     QStringLiteral("proxy configuration rejected"), kTimestamp, false, QString()),
                 QStringLiteral("\x1b[33m[2026-09-11 12:00:00.000] warning daemon proxy configuration rejected\x1b[0m"));
        QCOMPARE(renderMirroredLine(StderrSink::Tty, QtCriticalMsg, QStringLiteral("daemon"),
                                     QStringLiteral("plasma-lyricsd is already running"), kTimestamp, false, QString()),
                 QStringLiteral("\x1b[31m[2026-09-11 12:00:00.000] critical daemon plasma-lyricsd is already running\x1b[0m"));
        QCOMPARE(renderMirroredLine(StderrSink::Tty, QtFatalMsg, QStringLiteral("daemon"),
                                     QStringLiteral("assertion failed"), kTimestamp, false, QString()),
                 QStringLiteral("\x1b[1;31m[2026-09-11 12:00:00.000] fatal daemon assertion failed\x1b[0m"));
    }

    void rendersTtySinkWithEnvPattern()
    {
        // The env pattern's rendering replaces the body, but the ANSI wrap
        // is still layered on by severity -- QT_MESSAGE_PATTERN controls
        // the text, not whether a terminal gets colored.
        const QString patterned = QStringLiteral("ENVTEST|critical|plasmalyrics.daemon|already running");
        QCOMPARE(renderMirroredLine(StderrSink::Tty, QtCriticalMsg, QStringLiteral("daemon"),
                                     QStringLiteral("already running"), kTimestamp, true, patterned),
                 QStringLiteral("\x1b[31m") + patterned + QStringLiteral("\x1b[0m"));
    }

    void rendersJournaldSinkWithoutEnvPattern()
    {
        // No timestamp, no level word, just "<priority>category message".
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtInfoMsg, QStringLiteral("daemon"),
                                     QStringLiteral("started"), kTimestamp, false, QString()),
                 QStringLiteral("<6>daemon started"));
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtWarningMsg, QStringLiteral("daemon"),
                                     QStringLiteral("proxy configuration rejected"), kTimestamp, false, QString()),
                 QStringLiteral("<4>daemon proxy configuration rejected"));
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtCriticalMsg, QStringLiteral("daemon"),
                                     QStringLiteral("plasma-lyricsd is already running"), kTimestamp, false, QString()),
                 QStringLiteral("<3>daemon plasma-lyricsd is already running"));
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtFatalMsg, QStringLiteral("daemon"),
                                     QStringLiteral("assertion failed"), kTimestamp, false, QString()),
                 QStringLiteral("<2>daemon assertion failed"));
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtDebugMsg, QStringLiteral("resolver"),
                                     QStringLiteral("cache mapping missing"), kTimestamp, false, QString()),
                 QStringLiteral("<7>resolver cache mapping missing"));
    }

    void rendersJournaldSinkWithEnvPattern()
    {
        // The "<N>" prefix survives QT_MESSAGE_PATTERN -- journald cannot
        // parse pattern text, only that leading marker -- but the body
        // after it becomes the env pattern's own rendering verbatim.
        const QString patterned = QStringLiteral("ENVTEST|info|plasmalyrics.daemon|started");
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtInfoMsg, QStringLiteral("daemon"),
                                     QStringLiteral("started"), kTimestamp, true, patterned),
                 QStringLiteral("<6>") + patterned);
    }
};

QTEST_GUILESS_MAIN(LogMirrorFormatTest)
#include "tst_logmirrorformat.moc"
