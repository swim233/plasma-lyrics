#pragma once

#include <QString>

namespace PlasmaLyrics {

// Renders a log field value as a double-quoted string: backslash and
// double-quote are escaped, newline/carriage return are collapsed to their
// two-character escapes. Some upstream error strings (Qt's own "Protocol
// "%1" is unknown", for one) already carry double quotes, which would
// otherwise break the key=value line they land in. Lives in core/ rather
// than under providers/ or daemon/ because it only depends on QString and
// both the provider and daemon logging call sites need it.
inline QString quoted(const QString &value)
{
    QString result;
    result.reserve(value.size() + 2);
    result.append(QLatin1Char('"'));
    for (const QChar &ch : value) {
        if (ch == QLatin1Char('\\')) {
            result.append(QLatin1String("\\\\"));
        } else if (ch == QLatin1Char('"')) {
            result.append(QLatin1String("\\\""));
        } else if (ch == QLatin1Char('\n')) {
            result.append(QLatin1String("\\n"));
        } else if (ch == QLatin1Char('\r')) {
            result.append(QLatin1String("\\r"));
        } else {
            result.append(ch);
        }
    }
    result.append(QLatin1Char('"'));
    return result;
}

} // namespace PlasmaLyrics
