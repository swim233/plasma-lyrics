// SPDX-License-Identifier: GPL-2.0-or-later

#include "lyric/LrcParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace Lyrics
{
namespace
{

// `[mm:ss.fraction]`, 1-3 fraction digits, several timestamps per line.
const QRegularExpression kTimestamp(QStringLiteral("\\[(\\d+):(\\d{1,2})(?:\\.(\\d{1,3}))?\\]"));

// JSON credit line: `{"t":933,"c":[{"tx":"作曲: "},{"tx":"闹闹丶"}]}`.
LyricLine jsonCreditLine(const QJsonObject &o)
{
    LyricLine line;
    line.startMs = o.value(QLatin1String("t")).toVariant().toLongLong();
    QStringList tx;
    const auto arr = o.value(QLatin1String("c")).toArray();
    for (const auto &c : arr) {
        tx << c.toObject().value(QLatin1String("tx")).toString();
    }
    line.text = tx.join(QString());
    return line;
}

} // namespace

LrcParseResult parseLrc(const QString &lrc)
{
    LrcParseResult r;

    qint64 offsetShift = 0;
    const QRegularExpression kOffsetTag(QStringLiteral("\\[offset:(-?\\d+)\\]"));
    const QRegularExpressionMatch om = kOffsetTag.match(lrc);
    if (om.hasMatch()) {
        offsetShift = om.captured(1).toLongLong();
    }

    LyricLines lines;
    const QStringList raw = lrc.split(QLatin1Char('\n'));
    for (const QString &s : raw) {
        if (s.startsWith(QLatin1Char('{'))) {
            const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8());
            if (d.isObject()) {
                LyricLine line = jsonCreditLine(d.object());
                line.startMs += offsetShift;
                if (!line.text.trimmed().isEmpty()) {
                    lines << line;
                }
                continue;
            }
        }

        // Every timestamp on the line re-emits the text after the last one.
        QRegularExpressionMatchIterator it = kTimestamp.globalMatch(s);
        QList<qint64> stamps;
        qsizetype textBegin = -1;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            qint64 ms = m.captured(1).toLongLong() * 60000 + m.captured(2).toLongLong() * 1000;
            if (m.hasCaptured(3)) {
                qint64 frac = m.captured(3).toLongLong();
                switch (m.capturedLength(3)) {
                case 1: frac *= 100; break;
                case 2: frac *= 10; break;
                default: break; // 3 digits = milliseconds
                }
                ms += frac;
            }
            ms += offsetShift;
            stamps << ms;
            textBegin = m.capturedEnd(0);
        }
        if (stamps.isEmpty()) {
            continue;
        }
        const QString text = s.mid(textBegin).trimmed();
        if (text.isEmpty()) {
            continue;
        }
        for (const qint64 ms : stamps) {
            LyricLine line;
            line.startMs = ms;
            line.endMs = ms; // closed up below, from the next line's start
            line.text = text;
            lines << line;
        }
    }

    // Sort by instant: multiple timestamps on one line and out-of-order files
    // both arrive unsorted, and the timeline needs order.
    std::sort(lines.begin(), lines.end(), [](const LyricLine &a, const LyricLine &b) {
        return a.startMs < b.startMs;
    });

    // Dedupe by instant: a repeated timestamp would make the frontend flip the
    // animation twice at one instant.
    LyricLines deduped;
    deduped.reserve(lines.size());
    for (const LyricLine &line : lines) {
        if (!deduped.isEmpty() && deduped.last().startMs == line.startMs) {
            continue;
        }
        deduped << line;
    }

    // endMs = next line's start; the last line stays "open" (endMs == startMs)
    // and the frontend holds it until the track ends.
    for (qsizetype i = 0; i + 1 < deduped.size(); ++i) {
        deduped[i].endMs = deduped[i + 1].startMs;
    }

    r.lines = deduped;
    r.ok = true;
    return r;
}

} // namespace Lyrics
