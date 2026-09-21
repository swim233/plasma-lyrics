#include "settingslog.h"

#include "core/config/proxyspec.h"
#include "frontendlogging.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QStringList>

namespace PlasmaLyrics {

namespace {

QDBusMessage controlCall(const QString &method)
{
    return QDBusMessage::createMethodCall(
        QStringLiteral("io.github.swim233.PlasmaLyrics"),
        QStringLiteral("/io/github/swim233/PlasmaLyrics"),
        QStringLiteral("io.github.swim233.PlasmaLyrics.Control"),
        method);
}

void forward(const QDBusMessage &message)
{
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}

} // namespace

QString renderConfigValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::QStringList:
        return value.toStringList().join(QLatin1Char(','));
    default: {
        // QString verbatim; integral types render as decimal. A type
        // QVariant cannot convert to QString at all (a future Color or Font
        // kcfg entry, a QVariantList) would otherwise render empty for both
        // old and new and suppress the line entirely, so fall back to
        // QDebug's rendering rather than lose the change. Types that merely
        // stringify to an empty text (an empty QByteArray or QUrl) keep that
        // empty rendering: for them "" is the faithful value, and a QDebug
        // fallback would make an unchanged empty value look changed, and an
        // invalid QVariant (an absent key) is likewise "" rather than
        // "QVariant(Invalid)".
        QString text = value.toString();
        if (text.isEmpty() && value.isValid() && !value.canConvert<QString>()) {
            QDebug(&text).nospace() << value;
        }
        return text;
    }
    }
}

QString renderProxyUrlForLog(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    if (const auto spec = ProxySpec::parse(trimmed)) {
        return spec->display();
    }
    const QString redacted = ProxySpec::redactedForLog(trimmed);
    if (!redacted.isEmpty()) {
        return redacted;
    }
    return QStringLiteral("redacted(length=%1)").arg(trimmed.length());
}

void reportConfigChange(const ConfigChange &change)
{
    qCInfo(lcConfig).noquote() << configChangedLine(change);
    QDBusMessage message = controlCall(QStringLiteral("NoteConfigChange"));
    message << change.store << change.applet << change.form << change.key
            << change.oldValue << change.newValue;
    forward(message);
}

void reportSaveFailed(const QString &store, const QString &reason)
{
    qCWarning(lcConfig).noquote() << configSaveFailedLine(store, reason);
    QDBusMessage message = controlCall(QStringLiteral("NoteSaveFailed"));
    message << store << reason;
    forward(message);
}

void reportRestartRequested()
{
    qCInfo(lcConfig).noquote() << restartRequestedLine();
    forward(controlCall(QStringLiteral("NoteRestartRequested")));
}

void reportRestartFinished(bool success, const QString &error)
{
    if (success) {
        qCInfo(lcConfig).noquote() << restartFinishedLine(true, error);
    } else {
        qCWarning(lcConfig).noquote() << restartFinishedLine(false, error);
    }
    QDBusMessage message = controlCall(QStringLiteral("NoteRestartFinished"));
    message << success << error;
    forward(message);
}

} // namespace PlasmaLyrics
