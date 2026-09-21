#include "configchangelogger.h"

#include "settingslog.h"

namespace PlasmaLyrics {

ConfigChangeLogger::ConfigChangeLogger(QObject *parent)
    : QObject(parent)
{
}

QQmlPropertyMap *ConfigChangeLogger::configuration() const
{
    return m_configuration;
}

void ConfigChangeLogger::setConfiguration(QQmlPropertyMap *configuration)
{
    if (m_configuration == configuration) {
        return;
    }
    if (m_configuration) {
        disconnect(m_configuration, &QQmlPropertyMap::valueChanged,
                   this, &ConfigChangeLogger::onValueChanged);
    }
    m_configuration = configuration;
    m_snapshot.clear();
    if (m_configuration) {
        const QStringList keys = m_configuration->keys();
        for (const QString &key : keys) {
            m_snapshot.insert(key, renderConfigValue(m_configuration->value(key)));
        }
        connect(m_configuration, &QQmlPropertyMap::valueChanged,
                this, &ConfigChangeLogger::onValueChanged);
    }
    Q_EMIT configurationChanged();
}

uint ConfigChangeLogger::appletId() const
{
    return m_appletId;
}

void ConfigChangeLogger::setAppletId(uint appletId)
{
    if (m_appletId == appletId) {
        return;
    }
    m_appletId = appletId;
    Q_EMIT appletIdChanged();
}

QString ConfigChangeLogger::form() const
{
    return m_form;
}

void ConfigChangeLogger::setForm(const QString &form)
{
    if (m_form == form) {
        return;
    }
    m_form = form;
    Q_EMIT formChanged();
}

void ConfigChangeLogger::onValueChanged(const QString &key, const QVariant &value)
{
    const QString rendered = renderConfigValue(value);
    const auto it = m_snapshot.constFind(key);
    const bool hadEntry = it != m_snapshot.constEnd();
    if (hadEntry && it.value() == rendered) {
        return;
    }
    const QString oldValue = hadEntry ? it.value() : QString();
    m_snapshot.insert(key, rendered);
    reportConfigChange({.store = QStringLiteral("applet"),
                         .applet = QString::number(m_appletId),
                         .form = m_form,
                         .key = key,
                         .oldValue = oldValue,
                         .newValue = rendered});
}

} // namespace PlasmaLyrics
