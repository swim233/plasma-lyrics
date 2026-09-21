#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QString>
#include <QVariant>

namespace PlasmaLyrics {

// Observes one plasmoid instance's kcfg-backed configuration and reports
// every change as store="applet" (DESIGN.md decision 75 / configlog.h).
//
// This lives in the applet rather than in each individual settings page
// because Plasmoid.configuration is shared by all three config pages and
// both form factors, and because TextPolicy.migrateConfiguration() (run
// from main.qml's Component.onCompleted) can also write keys
// programmatically -- one hook on the property map itself catches every
// write regardless of which page, which form factor, or which code path
// produced it, without each page having to instrument its own controls.
//
// Plasmoid.configuration is a KConfigPropertyMap (KF6 KConfigQml), a
// QQmlPropertyMap subclass whose valueChanged(key, value) signal carries
// only the new value -- there is no old value to compare against. The
// snapshot below is what makes "read the current value once when the map is
// attached, then diff against that on every signal" possible: it is filled
// from configuration->keys() the moment configuration is set, and kept in
// sync as each valueChanged fires, so every change can be reported as an
// (old, new) pair even though the signal itself never carries one.
class ConfigChangeLogger : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQmlPropertyMap *configuration READ configuration WRITE setConfiguration
                   NOTIFY configurationChanged)
    Q_PROPERTY(uint appletId READ appletId WRITE setAppletId NOTIFY appletIdChanged)
    Q_PROPERTY(QString form READ form WRITE setForm NOTIFY formChanged)

public:
    explicit ConfigChangeLogger(QObject *parent = nullptr);

    QQmlPropertyMap *configuration() const;
    void setConfiguration(QQmlPropertyMap *configuration);

    uint appletId() const;
    void setAppletId(uint appletId);

    QString form() const;
    void setForm(const QString &form);

Q_SIGNALS:
    void configurationChanged();
    void appletIdChanged();
    void formChanged();

private:
    void onValueChanged(const QString &key, const QVariant &value);

    // QPointer rather than a raw pointer: nothing in main.qml destroys the
    // map before this object today (Plasmoid.configuration outlives the
    // applet), but there is no language guarantee of that, and a dangling
    // pointer here would make setConfiguration() disconnect from -- or a
    // still-pending signal deliver to -- freed memory. QPointer reads back
    // as null once the map is destroyed, so that path degrades to "no
    // configuration" instead of undefined behaviour.
    QPointer<QQmlPropertyMap> m_configuration;
    uint m_appletId = 0;
    QString m_form;
    // Keyed by config key, value is the already-rendered string
    // (renderConfigValue()), never the raw QVariant -- comparing rendered
    // text is what "identical re-assignments produce nothing" needs, since
    // two QVariants that render the same text (e.g. int 34 vs QString "34")
    // must not be treated as a change.
    QHash<QString, QString> m_snapshot;
};

} // namespace PlasmaLyrics
