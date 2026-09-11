#include "logmirrorformat.h"

namespace PlasmaLyrics {

QString displayCategory(const char *category)
{
    QString name = QString::fromUtf8(category);
    static const QString prefix = QStringLiteral("plasmalyrics.");
    if (name.startsWith(prefix)) {
        name.remove(0, prefix.size());
    }
    return name;
}

QLatin1String typeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QLatin1String("debug");
    case QtInfoMsg: return QLatin1String("info");
    case QtWarningMsg: return QLatin1String("warning");
    case QtCriticalMsg: return QLatin1String("critical");
    case QtFatalMsg: return QLatin1String("fatal");
    }
    return QLatin1String("info");
}

int syslogPriority(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return 7;
    case QtInfoMsg: return 6;
    case QtWarningMsg: return 4;
    case QtCriticalMsg: return 3;
    case QtFatalMsg: return 2;
    }
    return 6;
}

QLatin1String ansiColor(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QLatin1String("\x1b[2m");
    case QtInfoMsg: return QLatin1String("");
    case QtWarningMsg: return QLatin1String("\x1b[33m");
    case QtCriticalMsg: return QLatin1String("\x1b[31m");
    case QtFatalMsg: return QLatin1String("\x1b[1;31m");
    }
    return QLatin1String("");
}

QString plainLine(const QString &timestamp, QtMsgType type, const QString &category, const QString &message)
{
    return QStringLiteral("[%1] %2 %3 %4").arg(timestamp, typeName(type), category, message);
}

QString renderMirroredLine(StderrSink sink, QtMsgType type, const QString &category,
                           const QString &message, const QString &timestamp,
                           bool honorEnvPattern, const QString &patternedBody)
{
    switch (sink) {
    case StderrSink::Journald: {
        // No timestamp, no level word: journald already stores both per
        // entry, and repeating them here is exactly the duplication this
        // mode exists to remove. Milliseconds are deliberately not added
        // either -- journald keeps microsecond precision on its own,
        // available via `journalctl -o short-precise` when it is needed.
        const QString body = honorEnvPattern
            ? patternedBody
            : QStringLiteral("%1 %2").arg(category, message);
        return QStringLiteral("<%1>%2").arg(QString::number(syslogPriority(type)), body);
    }
    case StderrSink::Tty: {
        const QString line = honorEnvPattern ? patternedBody : plainLine(timestamp, type, category, message);
        const QLatin1String color = ansiColor(type);
        return color.isEmpty() ? line : color + line + QLatin1String("\x1b[0m");
    }
    case StderrSink::Plain:
        return honorEnvPattern ? patternedBody : plainLine(timestamp, type, category, message);
    }
    return honorEnvPattern ? patternedBody : plainLine(timestamp, type, category, message);
}

} // namespace PlasmaLyrics
