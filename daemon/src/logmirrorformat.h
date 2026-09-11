#pragma once

#include <QString>
#include <QtGlobal>

namespace PlasmaLyrics {

// Which of mirrorMessage's three output shapes stderr gets, decided once at
// startup by detectStderrSink() (main.cpp -- that part does real I/O via
// isatty()/fstat() and stays there). The log-file mirror always renders
// with StderrSink::Plain regardless of which of these stderr is actually
// using.
enum class StderrSink { Tty, Journald, Plain };

// The real QLoggingCategory name (e.g. "plasmalyrics.resolver") is what
// QT_LOGGING_RULES, plasma-lyricsd.categories and any hand-written
// qtlogging.ini rule match against, so it must never change -- only what a
// human reads on a terminal or in the journal is shortened here.
QString displayCategory(const char *category);

QLatin1String typeName(QtMsgType type);

// journald's own syslog priority field is shown natively (colored,
// filterable via `journalctl -p`) once a line carries a "<N>" prefix -- see
// sd-daemon(3), "Log Levels and Priorities for Stream-Based Logging".
int syslogPriority(QtMsgType type);

QLatin1String ansiColor(QtMsgType type);

// The one format shared by the log file and by stderr whenever it is
// neither a terminal nor journald: timestamp and level are spelled out
// because nothing else -- no journal metadata, no human at a terminal --
// carries them there.
QString plainLine(const QString &timestamp, QtMsgType type, const QString &category, const QString &message);

// Renders the line mirrorMessage writes for the given sink. category is
// already display-stripped; message is the raw Qt message text.
// honorEnvPattern/patternedBody carry QT_MESSAGE_PATTERN's effect: when
// honorEnvPattern is true, patternedBody -- the caller's
// qFormatLogMessage(type, context, message) result, rendered against the
// ORIGINAL, unstripped QMessageLogContext -- replaces the hand-rolled body
// below verbatim, category included: Qt's own %{category} means the real
// QLoggingCategory name, and QT_MESSAGE_PATTERN is an explicit opt-in to
// Qt's own raw rendering, so this deliberately does not re-strip it.
// journald's "<N>" prefix is still layered on separately in that case,
// since journald cannot parse pattern text, only that leading marker.
// Passing StderrSink::Plain is also what the log-file mirror should call,
// regardless of the stderr sink actually detected.
QString renderMirroredLine(StderrSink sink, QtMsgType type, const QString &category,
                           const QString &message, const QString &timestamp,
                           bool honorEnvPattern, const QString &patternedBody);

} // namespace PlasmaLyrics
