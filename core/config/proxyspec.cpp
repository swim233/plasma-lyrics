#include "proxyspec.h"

#include <QUrl>

namespace PlasmaLyrics {

std::optional<ProxySpec> ProxySpec::parse(const QString &url, Error *error)
{
    const auto fail = [error](Error reason) -> std::optional<ProxySpec> {
        if (error) *error = reason;
        return std::nullopt;
    };

    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid()) {
        return fail(Error::InvalidUrl);
    }

    Type type;
    const QString scheme = parsed.scheme().toLower();
    if (scheme == QStringLiteral("socks5")) {
        type = Type::Socks5;
    } else if (scheme == QStringLiteral("http")) {
        type = Type::Http;
    } else {
        // https, socks4 and socks5h are all rejected here too: none of them
        // is one of the two schemes this daemon knows how to apply.
        return fail(Error::UnsupportedScheme);
    }

    const QString host = parsed.host(QUrl::FullyDecoded);
    if (host.isEmpty()) {
        return fail(Error::MissingHost);
    }

    // port(-1) only returns a non-negative value when the URL spelled one
    // out explicitly, so this one check rejects both "no port" and the
    // syntactically-valid-but-out-of-range "port 0" as MissingPort. A port
    // above 65535 never reaches here: QUrl::StrictMode already refuses to
    // parse it, so that case surfaces as InvalidUrl instead.
    const int port = parsed.port(-1);
    if (port < 1) {
        return fail(Error::MissingPort);
    }

    // A bare trailing slash is how most people type an authority-only URL by
    // habit, so it is accepted alongside the empty path; anything deeper
    // ("/x", "//") still means the address carries an unexpected path.
    const QString path = parsed.path();
    if ((!path.isEmpty() && path != QStringLiteral("/"))
        || parsed.hasQuery() || parsed.hasFragment()) {
        return fail(Error::UnexpectedPath);
    }

    ProxySpec spec;
    spec.m_type = type;
    spec.m_host = host;
    spec.m_port = static_cast<quint16>(port);
    spec.m_user = parsed.userName(QUrl::FullyDecoded);
    spec.m_password = parsed.password(QUrl::FullyDecoded);
    if (error) *error = Error::None;
    return spec;
}

QString ProxySpec::redactedForLog(const QString &raw)
{
    const QUrl parsed(raw.trimmed(), QUrl::StrictMode);
    // No authority means QUrl found no scheme://host[:port] to anchor on --
    // most commonly a missing "//" (the scheme becomes opaque, and whatever
    // followed the colon, credentials included, becomes an unparsed path)
    // or no scheme at all. Either way there is nothing left to safely show.
    if (!parsed.isValid() || parsed.authority().isEmpty()) {
        return QString();
    }

    QUrl safe;
    safe.setScheme(parsed.scheme());
    const QString host = parsed.host(QUrl::FullyDecoded);
    if (!host.isEmpty()) {
        safe.setHost(host);
    }
    const int port = parsed.port(-1);
    if (port >= 0) {
        safe.setPort(port);
    }
    // Path, query and fragment are never read here, so credentials stuffed
    // into any of them (a common way authority-less input still slips
    // through, e.g. "scheme://h:p/user:pw@x" or "scheme://h:p?pw=...") never
    // reach the result -- there is nothing to strip because nothing was
    // copied in the first place.
    const QString result = safe.toString();
    // Host/port can never legitimately contain "@"; its presence would mean
    // something unexpected leaked through despite the above.
    return result.contains(QLatin1Char('@')) ? QString() : result;
}

QString ProxySpec::display() const
{
    // Built through QUrl rather than string concatenation so an IPv6 host
    // round-trips back through its bracketed form ("[::1]"); setHost() alone
    // never carries user() or password() along, so this stays credential-free
    // without having to strip anything.
    QUrl url;
    url.setScheme(m_type == Type::Socks5 ? QStringLiteral("socks5") : QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    return url.toString();
}

} // namespace PlasmaLyrics
