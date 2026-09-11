#include "core/config/proxyspec.h"

#include <QTest>

using namespace PlasmaLyrics;

class ProxySpecTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesValidSocks5Address()
    {
        ProxySpec::Error error = ProxySpec::Error::UnsupportedScheme;
        const auto spec = ProxySpec::parse(QStringLiteral("socks5://127.0.0.1:1080"), &error);
        QVERIFY(spec.has_value());
        QCOMPARE(error, ProxySpec::Error::None);
        QCOMPARE(spec->type(), ProxySpec::Type::Socks5);
        QCOMPARE(spec->host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(spec->port(), quint16(1080));
        QVERIFY(spec->user().isEmpty());
        QVERIFY(spec->password().isEmpty());
    }

    void parsesValidHttpAddress()
    {
        const auto spec = ProxySpec::parse(QStringLiteral("http://proxy.example.test:8080"));
        QVERIFY(spec.has_value());
        QCOMPARE(spec->type(), ProxySpec::Type::Http);
        QCOMPARE(spec->host(), QStringLiteral("proxy.example.test"));
        QCOMPARE(spec->port(), quint16(8080));
    }

    void decodesPercentEncodedCredentials()
    {
        const auto spec = ProxySpec::parse(QStringLiteral("socks5://user:pa%40ss@127.0.0.1:1080"));
        QVERIFY(spec.has_value());
        QCOMPARE(spec->user(), QStringLiteral("user"));
        QCOMPARE(spec->password(), QStringLiteral("pa@ss"));
    }

    void schemeIsCaseInsensitive()
    {
        const auto spec = ProxySpec::parse(QStringLiteral("HTTP://Host:80"));
        QVERIFY(spec.has_value());
        QCOMPARE(spec->type(), ProxySpec::Type::Http);
        QCOMPARE(spec->host(), QStringLiteral("host"));
    }

    void rejectsMissingPort()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://host"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::MissingPort);
    }

    void rejectsPortZero()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://host:0"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::MissingPort);
    }

    void rejectsOutOfRangePortAsInvalidUrl()
    {
        // QUrl::StrictMode already refuses to parse a port above 65535, so
        // this never reaches the explicit 1-65535 range check.
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://host:99999"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::InvalidUrl);
    }

    void rejectsMissingHost()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://:80"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::MissingHost);
    }

    void rejectsHttpsScheme()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("https://host:443"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::UnsupportedScheme);
    }

    void rejectsSocks5hScheme()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("socks5h://host:1080"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::UnsupportedScheme);
    }

    void acceptsBareTrailingSlash()
    {
        ProxySpec::Error error = ProxySpec::Error::UnexpectedPath;
        const auto spec = ProxySpec::parse(QStringLiteral("http://127.0.0.1:7890/"), &error);
        QVERIFY(spec.has_value());
        QCOMPARE(error, ProxySpec::Error::None);
        // display() never carries a path component regardless of what the
        // input's path was, so this also covers "no trailing slash leaks
        // through here".
        QCOMPARE(spec->display(), QStringLiteral("http://127.0.0.1:7890"));
    }

    void rejectsDeepPath()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://127.0.0.1:7890/x"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::UnexpectedPath);
    }

    void rejectsDoubleSlashPath()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://127.0.0.1:7890//"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::UnexpectedPath);
    }

    void rejectsQuery()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("http://host:80?a=b"), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::UnexpectedPath);
    }

    void rejectsEmptyString()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QString(), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::InvalidUrl);
    }

    void rejectsWhitespaceOnlyString()
    {
        ProxySpec::Error error = ProxySpec::Error::None;
        const auto spec = ProxySpec::parse(QStringLiteral("   \t  "), &error);
        QVERIFY(!spec.has_value());
        QCOMPARE(error, ProxySpec::Error::InvalidUrl);
    }

    void trimsSurroundingWhitespace()
    {
        const auto spec = ProxySpec::parse(QStringLiteral("  socks5://host:1080  "));
        QVERIFY(spec.has_value());
        QCOMPARE(spec->host(), QStringLiteral("host"));
    }

    void displayNeverIncludesCredentials()
    {
        const auto spec = ProxySpec::parse(QStringLiteral("socks5://user:secret@host:1080"));
        QVERIFY(spec.has_value());
        const QString shown = spec->display();
        QCOMPARE(shown, QStringLiteral("socks5://host:1080"));
        QVERIFY(!shown.contains(QStringLiteral("user")));
        QVERIFY(!shown.contains(QStringLiteral("secret")));
    }

    void displayBracketsAnIpv6Host()
    {
        const auto spec = ProxySpec::parse(QStringLiteral("socks5://[::1]:1080"));
        QVERIFY(spec.has_value());
        QCOMPARE(spec->display(), QStringLiteral("socks5://[::1]:1080"));
    }

    // qa-2 found that main.cpp's inline redaction (QUrl::RemoveUserInfo on
    // whatever QUrl made of the raw string) leaked "user:password" whenever
    // QUrl could not recognize an authority component at all -- most often a
    // missing "//" -- because the credentials end up inside an opaque path
    // instead of the userinfo subcomponent RemoveUserInfo strips. These five
    // inputs are the ones that were shown to leak (or could plausibly leak
    // the same way): none of them may ever contain the password substring.
    void redactedForLogNeverLeaksCredentials()
    {
        const QString needle = QStringLiteral("hunter2");
        QVERIFY(!ProxySpec::redactedForLog(QStringLiteral("socks5://alice:hunter2@h:1080"))
                     .contains(needle));
        QVERIFY(!ProxySpec::redactedForLog(QStringLiteral("socks5:alice:hunter2@h:1080"))
                     .contains(needle));
        QVERIFY(!ProxySpec::redactedForLog(QStringLiteral("alice:hunter2@h:1080"))
                     .contains(needle));
        QVERIFY(!ProxySpec::redactedForLog(QStringLiteral("socks5://h:1080/alice:hunter2@x"))
                     .contains(needle));
        QVERIFY(!ProxySpec::redactedForLog(QStringLiteral("socks5://h:1080?pw=hunter2"))
                     .contains(needle));
    }

    void redactedForLogShowsHostPortWhenCredentialedAndWellFormed()
    {
        // A well-formed, credentialed address is still safe to summarize --
        // only the credentials themselves are the problem, not the address.
        QCOMPARE(ProxySpec::redactedForLog(QStringLiteral("socks5://alice:hunter2@h:1080")),
                 QStringLiteral("socks5://h:1080"));
    }

    void redactedForLogKeepsPartialInfoWhenHostIsMissing()
    {
        // MissingHost still has a recognizable authority (a port, just no
        // host), so the port alone is safe and useful to log.
        QCOMPARE(ProxySpec::redactedForLog(QStringLiteral("socks5://user:pw@:1080")),
                 QStringLiteral("socks5://:1080"));
    }

    void redactedForLogIsEmptyWithoutARecognizableAuthority()
    {
        QVERIFY(ProxySpec::redactedForLog(QStringLiteral("socks5:alice:hunter2@h:1080")).isEmpty());
        QVERIFY(ProxySpec::redactedForLog(QStringLiteral("alice:hunter2@h:1080")).isEmpty());
        QVERIFY(ProxySpec::redactedForLog(QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ProxySpecTest)
#include "tst_proxyspec.moc"
