// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "provider.h"

#include <QObject>

namespace Lyrics
{

// The first-version online source (DESIGN §1.2): plaintext GET search, LRC
// with leading JSON credit lines, no yrc reachable.
class NeteaseProvider : public QObject, public Lyrics::Provider
{
    Q_OBJECT
    Q_INTERFACES(Lyrics::Provider)

public:
    explicit NeteaseProvider(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("netease"); }
    bool isConfigured() const override { return true; } // stateless direct connection
};

} // namespace Lyrics

Q_DECLARE_INTERFACE(Lyrics::Provider, "io.github.swim233.Lyrics.Provider")
