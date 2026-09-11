#include "daemon/src/logmirrorformat.h"

#include <QStringList>
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

    // Matches Qt's own mapping (debug=7/info=6/warning=4/critical=2/
    // fatal=1), not an independently chosen scale: early log lines, before
    // main() installs mirrorMessage, go through Qt's native journald sink
    // and already use these values, so a different mapping here would put
    // the same severity at two different priorities depending on which
    // handler happened to log it.
    void mapsSyslogPriorities()
    {
        QCOMPARE(syslogPriority(QtDebugMsg), 7);
        QCOMPARE(syslogPriority(QtInfoMsg), 6);
        QCOMPARE(syslogPriority(QtWarningMsg), 4);
        QCOMPARE(syslogPriority(QtCriticalMsg), 2);
        QCOMPARE(syslogPriority(QtFatalMsg), 1);
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
                 QStringLiteral("<2>daemon plasma-lyricsd is already running"));
        QCOMPARE(renderMirroredLine(StderrSink::Journald, QtFatalMsg, QStringLiteral("daemon"),
                                     QStringLiteral("assertion failed"), kTimestamp, false, QString()),
                 QStringLiteral("<1>daemon assertion failed"));
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

    // journald splits stdout/stderr on newlines and treats each physical
    // line as its own entry -- a prefix on only the first line would leave
    // every continuation line at journald's default priority instead of
    // the message's real one (this is exactly what the multi-line Qt
    // locale warning showed under manual review: the first line carried
    // the right PRIORITY, the next two did not). Every line must carry
    // its own "<N>" prefix, in both the default body and the
    // QT_MESSAGE_PATTERN-rendered body.
    void rendersJournaldSinkMultiLineMessage()
    {
        const QString message = QStringLiteral(
            "Detected locale \"C\" with character encoding \"ANSI_X3.4-1968\", which is not UTF-8.\n"
            "Qt depends on a UTF-8 locale, and has switched to \"C.UTF-8\" instead.\n"
            "If this causes problems, reconfigure your locale. See the locale(1) manual\n"
            "for more information.");
        const QString rendered = renderMirroredLine(StderrSink::Journald, QtWarningMsg,
                                                     QStringLiteral("default"), message, kTimestamp,
                                                     false, QString());
        const QStringList renderedLines = rendered.split(QLatin1Char('\n'));
        QCOMPARE(renderedLines.size(), 4);
        QCOMPARE(renderedLines.at(0), QStringLiteral("<4>default Detected locale \"C\" with character "
                                                       "encoding \"ANSI_X3.4-1968\", which is not UTF-8."));
        QCOMPARE(renderedLines.at(1), QStringLiteral("<4>Qt depends on a UTF-8 locale, and has switched "
                                                       "to \"C.UTF-8\" instead."));
        QCOMPARE(renderedLines.at(2), QStringLiteral("<4>If this causes problems, reconfigure your locale. "
                                                       "See the locale(1) manual"));
        QCOMPARE(renderedLines.at(3), QStringLiteral("<4>for more information."));
    }

    void rendersJournaldSinkMultiLineEnvPattern()
    {
        const QString patterned = QStringLiteral("ENVTEST|warning|default|line one\nline two\nline three");
        const QString rendered = renderMirroredLine(StderrSink::Journald, QtWarningMsg,
                                                     QStringLiteral("default"), QStringLiteral("unused"),
                                                     kTimestamp, true, patterned);
        QCOMPARE(rendered, QStringLiteral("<4>ENVTEST|warning|default|line one\n<4>line two\n<4>line three"));
    }
};

QTEST_GUILESS_MAIN(LogMirrorFormatTest)
#include "tst_logmirrorformat.moc"
