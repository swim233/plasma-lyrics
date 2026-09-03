// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace Lyrics
{

struct SearchReply
{
    bool ok = false;
    QString errorText;
    // Raw body; parsing lives in core/ so it is unit-testable offline.
    QByteArray body;
};

// Plain GET search endpoint (DESIGN §1.2): no cookie, no weapi encryption.
class NeteaseApi : public QObject
{
    Q_OBJECT
public:
    explicit NeteaseApi(QObject *parent = nullptr);
    ~NeteaseApi() override;

    void search(const QString &keyword, int limit = 5);

Q_SIGNALS:
    void finished(const Lyrics::SearchReply &reply);

private:
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace Lyrics
