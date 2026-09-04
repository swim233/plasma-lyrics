#pragma once

#include "mpristypes.h"

#include <QObject>
#include <QVariantMap>

namespace PlasmaLyrics {

class MprisPlayer final : public QObject
{
    Q_OBJECT

public:
    explicit MprisPlayer(QString service, QObject *parent = nullptr);

    MprisState state() const;
    void refresh();
    void pollPosition();
    static qint64 monotonicNowNs();

Q_SIGNALS:
    void changed(bool metadataChanged, bool anchorChanged, bool becamePlaying);

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface,
                             const QVariantMap &changedProperties,
                             const QStringList &invalidatedProperties);

private:
    static QVariantMap variantMap(const QVariant &value);
    static QStringList variantStringList(const QVariant &value);
    QVariantMap getAll(const QString &interface) const;
    void apply(const QVariantMap &properties, bool initial = false);

    MprisState m_state;
    qint64 m_lastSamplePositionUs = -1;
    qint64 m_lastSampleMonotonicNs = 0;
};

} // namespace PlasmaLyrics

