// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/ClipPreview.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/UiLanguage.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Image.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/WindowManager.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <cmath>

namespace chatterino::clippreview {

namespace {

constexpr int REQUEST_TIMEOUT_MS = 8000;
constexpr qsizetype MAX_CACHED = 300;

// The cards are only used from the GUI thread.
QHash<QString, std::shared_ptr<const ClipPreviewInfo>> &cache()
{
    static QHash<QString, std::shared_ptr<const ClipPreviewInfo>> value;
    return value;
}

QSet<QString> &asked()
{
    static QSet<QString> value;
    return value;
}

QHash<QString, QString> &gameNames()
{
    static QHash<QString, QString> value;
    return value;
}

void scheduleRelayout()
{
    // Several cards load together: one relayout of the chats for the batch.
    static bool queued = false;
    if (queued || tryGetApp() == nullptr)
    {
        return;
    }
    queued = true;
    QTimer::singleShot(150, QCoreApplication::instance(), [] {
        queued = false;
        if (auto *app = tryGetApp(); app && app->getWindows())
        {
            app->getWindows()->forceLayoutChannelViews();
        }
    });
}

void store(ClipPreviewInfo info)
{
    if (cache().size() >= MAX_CACHED)
    {
        cache().clear();  // rare; the cards are simply loaded again
    }
    auto slug = info.slug;
    cache().insert(slug, std::make_shared<const ClipPreviewInfo>(
                             std::move(info)));
    scheduleRelayout();
}

bool isRussian()
{
    return activeUiLanguage().startsWith(QStringLiteral("ru"));
}

}  // namespace

QString clipSlug(const QString &url)
{
    const QUrl parsed(url.contains(QStringLiteral("://"))
                          ? url
                          : QStringLiteral("https://") + url);
    auto host = parsed.host().toLower();
    if (host.startsWith(QStringLiteral("www.")) ||
        host.startsWith(QStringLiteral("m.")))
    {
        host = host.mid(host.indexOf('.') + 1);
    }
    const auto parts = parsed.path().split('/', Qt::SkipEmptyParts);
    QString slug;
    if (host == QStringLiteral("clips.twitch.tv") && parts.size() == 1)
    {
        slug = parts[0];
    }
    else if (host == QStringLiteral("twitch.tv") && parts.size() == 3 &&
             parts[1] == QStringLiteral("clip"))
    {
        slug = parts[2];
    }
    static const QRegularExpression valid(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{2,99}$"));
    if (!valid.match(slug).hasMatch() || slug == QStringLiteral("create") ||
        slug == QStringLiteral("embed"))
    {
        return {};
    }
    return slug;
}

std::shared_ptr<const ClipPreviewInfo> get(const QString &slug)
{
    return cache().value(slug);
}

void request(const QString &slug)
{
    if (slug.isEmpty() || asked().contains(slug) || tryGetApp() == nullptr)
    {
        return;
    }
    auto *accounts = getApp()->getAccounts();
    if (accounts == nullptr)
    {
        return;
    }
    auto account = accounts->twitch.getCurrent();
    // Helix needs a signed-in account; only the clip id goes to Twitch.
    if (!account || account->isAnon() || account->getOAuthToken().isEmpty())
    {
        return;
    }
    asked().insert(slug);

    NetworkRequest(QStringLiteral("https://api.twitch.tv/helix/clips?id=") +
                   QString::fromUtf8(QUrl::toPercentEncoding(slug)))
        .header("Client-ID", account->getOAuthClient())
        .header("Authorization",
                QStringLiteral("Bearer ") + account->getOAuthToken())
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess([slug](const NetworkResult &result) {
            const auto data = result.parseJson().value("data").toArray();
            if (data.isEmpty())
            {
                return;  // deleted clip: no card, the link stays
            }
            const auto clip = data.first().toObject();
            ClipPreviewInfo info;
            info.slug = slug;
            info.url = clip.value("url").toString();
            info.title = clip.value("title").toString();
            info.broadcaster = clip.value("broadcaster_name").toString();
            info.creator = clip.value("creator_name").toString();
            info.views = clip.value("view_count").toInt();
            info.durationSeconds = clip.value("duration").toDouble();
            info.createdAt = QDateTime::fromString(
                clip.value("created_at").toString(), Qt::ISODate);
            const auto thumbnail = clip.value("thumbnail_url").toString();
            const QUrl thumbnailUrl(thumbnail);
            const auto thumbnailHost = thumbnailUrl.host().toLower();
            if (thumbnailUrl.scheme() == QStringLiteral("https") &&
                (thumbnailHost.endsWith(QStringLiteral(".twitch.tv")) ||
                 thumbnailHost.endsWith(QStringLiteral(".jtvnw.net"))))
            {
                info.thumbnail = Image::fromUrl({thumbnail});
            }
            const auto gameId = clip.value("game_id").toString();
            if (gameId.isEmpty() || gameNames().contains(gameId))
            {
                info.gameName = gameNames().value(gameId);
                store(std::move(info));
                return;
            }
            auto shared = std::make_shared<ClipPreviewInfo>(std::move(info));
            getHelix()->getGameById(
                gameId,
                [shared, gameId](const HelixGame &game) {
                    gameNames().insert(gameId, game.name);
                    shared->gameName = game.name;
                    store(*shared);
                },
                [shared] {
                    store(*shared);  // the card without the game line
                });
        })
        .execute();
}

QString formatDuration(double seconds)
{
    const auto total = qMax(0, static_cast<int>(std::lround(seconds)));
    const auto h = total / 3600;
    const auto m = (total / 60) % 60;
    const auto s = total % 60;
    if (h > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

QString formatViews(int views)
{
    if (!isRussian())
    {
        return views == 1 ? QStringLiteral("1 view")
                          : QStringLiteral("%1 views").arg(views);
    }
    const auto n100 = views % 100;
    const auto n10 = views % 10;
    const char *word = "просмотров";
    if (n100 < 11 || n100 > 14)
    {
        if (n10 == 1)
        {
            word = "просмотр";
        }
        else if (n10 >= 2 && n10 <= 4)
        {
            word = "просмотра";
        }
    }
    return QStringLiteral("%1 %2").arg(views).arg(QString::fromUtf8(word));
}

QString formatAge(const QDateTime &createdAt, const QDateTime &now)
{
    if (!createdAt.isValid())
    {
        return {};
    }
    const auto secs = qMax<qint64>(0, createdAt.secsTo(now));
    const bool ru = isRussian();
    if (secs < 60)
    {
        return ru ? QString::fromUtf8("только что") : QStringLiteral("just now");
    }
    if (secs < 3600)
    {
        return (ru ? QString::fromUtf8("%1 мин. назад")
                   : QStringLiteral("%1 min ago"))
            .arg(secs / 60);
    }
    if (secs < 86400)
    {
        return (ru ? QString::fromUtf8("%1 ч назад") : QStringLiteral("%1 h ago"))
            .arg(secs / 3600);
    }
    if (secs < 86400 * 365)
    {
        return (ru ? QString::fromUtf8("%1 дн. назад")
                   : QStringLiteral("%1 d ago"))
            .arg(secs / 86400);
    }
    return QLocale(ru ? QLocale::Russian : QLocale::English)
        .toString(createdAt.toLocalTime().date(), QLocale::ShortFormat);
}

void insertForTesting(ClipPreviewInfo info)
{
    asked().insert(info.slug);
    store(std::move(info));
}

}  // namespace chatterino::clippreview
