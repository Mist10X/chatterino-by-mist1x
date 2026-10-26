#include "providers/twitch/UsercardBadges.hpp"

#include "messages/Image.hpp"
#include "util/PostToThread.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUrl>

#include <algorithm>

namespace chatterino {
namespace {

struct Subscriber {
    QPointer<QObject> context;
    std::function<void(QVector<GqlBadge>)> success;
    std::function<void(const QString &)> failure;
};

struct CacheEntry {
    QVector<GqlBadge> badges;
    QString error;
    qint64 expiresAt{};
};

// All cache and in-flight state is confined to the GUI thread. Callbacks never
// retain a card, account, or OAuth token after the request has completed.
QHash<QString, CacheEntry> cache;
QHash<QString, QVector<Subscriber>> pending;
QElapsedTimer clock;

qint64 now()
{
    if (!clock.isValid())
    {
        clock.start();
    }
    return clock.elapsed();
}

void deliver(const Subscriber &subscriber, const CacheEntry &entry)
{
    if (!subscriber.context)
    {
        return;
    }
    if (entry.error.isEmpty())
    {
        subscriber.success(entry.badges);
    }
    else
    {
        subscriber.failure(entry.error);
    }
}

void finish(const QString &key, QVector<GqlBadge> badges, QString error)
{
    const auto expiresAt = now() + (error.isEmpty() ? 300000 : 15000);
    const CacheEntry entry{std::move(badges), std::move(error),
                           expiresAt};
    if (cache.size() >= 128 && !cache.contains(key))
    {
        auto oldest = std::min_element(
            cache.begin(), cache.end(), [](const auto &a, const auto &b) {
                return a.expiresAt < b.expiresAt;
            });
        cache.erase(oldest);
    }
    cache.insert(key, entry);
    const auto subscribers = pending.take(key);
    for (const auto &subscriber : subscribers)
    {
        deliver(subscriber, entry);
    }
}

}  // namespace

void requestUsercardBadges(
    const QString &userId, const QString &login, const QString &channel,
    QObject *context, std::function<void(QVector<GqlBadge>)> success,
    std::function<void(const QString &)> failure)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    const Subscriber subscriber{context, std::move(success), std::move(failure)};
    static const QRegularExpression uid(QStringLiteral("^[0-9]{1,20}$"));
    static const QRegularExpression name(QStringLiteral("^[a-z0-9_]{1,25}$"));
    const auto userLogin = login.trimmed().toLower();
    const auto channelLogin = channel.trimmed().toLower();
    if (!context || !uid.match(userId).hasMatch() ||
        !name.match(userLogin).hasMatch() || !name.match(channelLogin).hasMatch())
    {
        deliver(subscriber, {{}, QStringLiteral("Не удалось определить пользователя и канал"), 0});
        return;
    }

    const auto key = userId + QChar('|') + userLogin +
                     QChar('|') + channelLogin;
    const auto cached = cache.constFind(key);
    if (cached != cache.cend() && cached->expiresAt > now())
    {
        deliver(subscriber, *cached);
        return;
    }
    if (auto active = pending.find(key); active != pending.end())
    {
        if (active->size() < 32)
        {
            active->push_back(subscriber);
        }
        else
        {
            deliver(subscriber, {{}, QStringLiteral("Запрос значков уже выполняется"), 0});
        }
        return;
    }
    if (pending.size() >= 8)
    {
        deliver(subscriber, {{}, QStringLiteral("Загрузка значков занята; откройте карточку ещё раз"), 0});
        return;
    }
    pending.insert(key, {subscriber});
    TwitchGql::getChannelViewerEarnedBadges(
        userLogin, channelLogin,
        QString{},
        [key](QVector<GqlBadge> badges) {
            postToThread([key, badges = std::move(badges)]() mutable {
                if (badges.size() > 256)
                {
                    badges.resize(256);
                }
                finish(key, std::move(badges), {});
            });
        },
        [key](const QString &error) {
            postToThread([key, error] { finish(key, {}, error); });
        },
        userId);
}

std::vector<EmotePtr> usercardBadgeEmotes(const QVector<GqlBadge> &badges)
{
    std::vector<EmotePtr> emotes;
    QSet<QString> seen;
    for (const auto &badge : badges)
    {
        const auto key = badge.setID.isEmpty()
                             ? badge.id
                             : badge.setID + QChar('/') + badge.version;
        if (key.isEmpty() || seen.contains(key))
        {
            continue;
        }
        auto image = [](const QString &url, qreal scale, int size) -> ImagePtr {
            const QUrl parsed(url);
            if (parsed.scheme() != QStringLiteral("https") || parsed.host().isEmpty() ||
                !parsed.userInfo().isEmpty())
            {
                return {};
            }
            return Image::fromUrl(Url{url}, scale, QSize(size, size));
        };
        auto one = image(badge.image1x, 1, 18);
        auto two = image(badge.image2x, .5, 36);
        auto four = image(badge.image4x, .25, 72);
        if (!one && !two && !four)
        {
            continue;
        }
        if (!one) one = two ? two : four;
        if (!two) two = four ? four : one;
        if (!four) four = two;
        seen.insert(key);
        auto tooltip = badge.title.toHtmlEscaped();
        if (!badge.description.isEmpty() && badge.description != badge.title)
        {
            tooltip += QStringLiteral("<br>") + badge.description.toHtmlEscaped();
        }
        emotes.push_back(std::make_shared<Emote>(Emote{
            .name = EmoteName{badge.title},
            .images = ImageSet{one, two, four},
            .tooltip = Tooltip{tooltip},
            .id = EmoteId{key},
        }));
    }
    return emotes;
}

}  // namespace chatterino
