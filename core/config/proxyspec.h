#pragma once

#include <QString>
#include <optional>

namespace PlasmaLyrics {

// Parses the `network/proxyUrl` ini value into a proxy the daemon can apply
// and the config UI can validate, without either side depending on
// QtNetwork. See DESIGN.md decision 63.
class ProxySpec
{
public:
    enum class Type { Socks5, Http };
    enum class Error {
        None,
        InvalidUrl,
        UnsupportedScheme,
        MissingHost,
        MissingPort,
        UnexpectedPath,
    };

    // Trims surrounding whitespace, then parses with QUrl::StrictMode.
    // Only socks5:// and http:// (case-insensitive) are accepted; the host
    // and an explicit 1-65535 port are required; the path must be empty or a
    // single "/", and query and fragment must both be empty. On failure
    // returns std::nullopt and, if error is non-null, sets *error to the
    // specific reason.
    static std::optional<ProxySpec> parse(const QString &url, Error *error = nullptr);

    // Best-effort, credential-free summary of an arbitrary manual proxy
    // address string, for logging once parse() has already rejected it (the
    // reason it failed is why the string cannot simply be run back through
    // parse() -- e.g. a missing "//" makes QUrl treat the whole thing as an
    // opaque path, so nothing is recognized as a host/port to validate).
    // Reconstructs only scheme/host/port on a fresh QUrl -- it never copies
    // the input's path, query, fragment or userinfo into the result, so
    // nothing else in the original string can leak through. Returns an
    // empty string when nothing can be shown safely (no recognizable
    // scheme://host[:port] authority at all, or -- belt and suspenders --
    // the reconstructed form still somehow carries an "@"); the caller
    // should fall back to logging only raw.length() in that case.
    static QString redactedForLog(const QString &raw);

    Type type() const { return m_type; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QString user() const { return m_user; }
    QString password() const { return m_password; }

    // "scheme://host:port" -- never includes user() or password(), so it is
    // always safe to log or display.
    QString display() const;

private:
    Type m_type = Type::Socks5;
    QString m_host;
    quint16 m_port = 0;
    QString m_user;
    QString m_password;
};

} // namespace PlasmaLyrics
