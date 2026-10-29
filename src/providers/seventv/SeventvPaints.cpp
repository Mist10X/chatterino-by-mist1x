#include "providers/seventv/SeventvPaints.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "messages/Image.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/seventv/paints/PaintDropShadow.hpp"
#include "providers/seventv/paints/RadialGradientPaint.hpp"
#include "providers/seventv/paints/UrlPaint.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "util/DebugCount.hpp"
#include "util/PostToThread.hpp"
#include "util/Variant.hpp"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>

#include <chrono>

namespace {
using namespace chatterino;
using namespace Qt::Literals::StringLiterals;

constexpr std::chrono::minutes SEVENTV_PAINT_FRAME_CACHE_LIFETIME{4};
/// A saved assignment 7TV hasn't confirmed for this long is dropped.
constexpr qint64 SAVED_ASSIGNMENT_LIFETIME_S = 14 * 24 * 3600;
constexpr int SAVE_DELAY_MS = 30 * 1000;
/// A paint 7TV confirmed this recently isn't looked up again.
constexpr qint64 LOOKUP_AFTER_S = 15 * 60;
constexpr int LOOKUP_INTERVAL_MS = 1000;
/// 7TV refuses a query over its complexity limit (12 chatters pass, 16 not).
constexpr size_t LOOKUP_BATCH = 10;
constexpr size_t LOOKUP_QUEUE_MAX = 2000;
const QString LOOKUP_URL = QStringLiteral("https://7tv.io/v3/gql");
const QString LOOKUP_FIELDS = QStringLiteral(
    "{style{paint_id paint{id name color function angle shape image_url "
    "repeat stops{at color} shadows{x_offset y_offset radius color}} "
    "badge_id badge{id name tooltip tag host{url files{name format width "
    "height}}}}}");

qint64 nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

QColor rgbaToQColor(const uint32_t color)
{
    auto red = (int)((color >> 24) & 0xFF);
    auto green = (int)((color >> 16) & 0xFF);
    auto blue = (int)((color >> 8) & 0xFF);
    auto alpha = (int)(color & 0xFF);

    return {red, green, blue, alpha};
}

std::optional<QColor> parsePaintColor(const QJsonValue &color)
{
    if (color.isNull())
    {
        return std::nullopt;
    }

    return rgbaToQColor(color.toInt());
}

QGradientStops parsePaintStops(const QJsonArray &stops)
{
    QGradientStops parsedStops;
    double lastStop = -1;

    for (const auto stop : stops)
    {
        const auto stopObject = stop.toObject();

        const auto rgbaColor = stopObject["color"].toInt();
        auto position = stopObject["at"].toDouble();

        if (position <= lastStop)
        {
            position = lastStop + 0.0000001;
        }

        lastStop = position;
        parsedStops.append(QGradientStop(position, rgbaToQColor(rgbaColor)));
    }

    return parsedStops;
}

std::vector<PaintDropShadow> parseDropShadows(const QJsonArray &dropShadows)
{
    std::vector<PaintDropShadow> parsedDropShadows;

    for (const auto shadow : dropShadows)
    {
        const auto shadowObject = shadow.toObject();

        const auto xOffset = shadowObject["x_offset"].toDouble();
        const auto yOffset = shadowObject["y_offset"].toDouble();
        const auto radius = shadowObject["radius"].toDouble();
        const auto rgbaColor = shadowObject["color"].toInt();

        parsedDropShadows.emplace_back(xOffset, yOffset, radius,
                                       rgbaToQColor(rgbaColor));
    }

    return parsedDropShadows;
}

std::optional<std::shared_ptr<Paint>> parsePaint(const QJsonObject &paintJson)
{
    const QString name = paintJson["name"].toString();
    const QString id = paintJson["id"].toString();

    const auto color = parsePaintColor(paintJson["color"]);
    const bool repeat = paintJson["repeat"].toBool();
    const float angle = (float)paintJson["angle"].toDouble();

    const QGradientStops stops = parsePaintStops(paintJson["stops"].toArray());

    const auto shadows = parseDropShadows(paintJson["shadows"].toArray());

    const QString function = paintJson["function"].toString();
    if (function == "LINEAR_GRADIENT" || function == "linear-gradient")
    {
        return std::make_shared<LinearGradientPaint>(name, id, color, stops,
                                                     repeat, angle, shadows);
    }

    if (function == "RADIAL_GRADIENT" || function == "radial-gradient")
    {
        return std::make_shared<RadialGradientPaint>(name, id, stops, repeat,
                                                     shadows);
    }

    if (function == "URL" || function == "url")
    {
        const QString url = paintJson["image_url"].toString();
        const ImagePtr image = Image::fromUrl({url}, 1);
        if (image == nullptr)
        {
            return std::nullopt;
        }
        image->setFrameCacheLifetime(SEVENTV_PAINT_FRAME_CACHE_LIFETIME);

        return std::make_shared<UrlPaint>(name, id, image, shadows);
    }

    return std::nullopt;
}

}  // namespace

namespace chatterino {

SeventvPaints::SeventvPaints() = default;

SeventvPaints::~SeventvPaints()
{
    if (this->dirty_)
    {
        this->save();
    }
}

void SeventvPaints::ensureLoaded() const
{
    std::call_once(this->loaded_, [this] {
        auto *self = const_cast<SeventvPaints *>(this);
        self->load();
        // Your own paint and badge first, before you write anything.
        auto account = getApp()->getAccounts()->twitch.getCurrent();
        if (account && !account->isAnon())
        {
            self->queueLookup(account->getUserName(), account->getUserId());
        }
    });
}

void SeventvPaints::load()
{
    this->file_ = QDir(getApp()->getPaths().miscDirectory)
                      .absoluteFilePath(QStringLiteral("seventv-paints.json"));

    QFile file(this->file_);
    if (!file.open(QFile::ReadOnly))
    {
        return;
    }
    const auto root = QJsonDocument::fromJson(file.readAll()).object();

    std::unique_lock lock(this->mutex_);

    const auto paints = root["paints"].toObject();
    for (auto it = paints.begin(); it != paints.end(); ++it)
    {
        const auto json = it.value().toObject();
        if (auto paint = parsePaint(json))
        {
            this->knownPaints_[it.key()] = *paint;
            this->paintJson_[it.key()] = json;
        }
    }

    const auto oldest = nowSeconds() - SAVED_ASSIGNMENT_LIFETIME_S;
    auto restore = [&](const QJsonObject &users, auto &map, auto &confirmed) {
        for (auto it = users.begin(); it != users.end(); ++it)
        {
            const auto user = it.value().toObject();
            const auto when =
                static_cast<qint64>(user["lastConfirmed"].toDouble());
            const auto paint =
                this->knownPaints_.find(user["paintId"].toString());
            if (when < oldest || paint == this->knownPaints_.end())
            {
                continue;
            }
            map[it.key()] = paint->second;
            confirmed[it.key()] = when;
        }
    };
    restore(root["twitch"].toObject(), this->twitchPaintMap_,
            this->twitchConfirmed_);
    restore(root["kick"].toObject(), this->kickPaintMap_,
            this->kickConfirmed_);

    const auto badges = root["badges"].toObject();
    for (auto it = badges.begin(); it != badges.end(); ++it)
    {
        this->badgeJson_[it.key()] = it.value().toObject();
    }
    auto *registry = getApp()->getSeventvBadges();
    const auto userBadges = root["twitchBadges"].toObject();
    for (auto it = userBadges.begin(); it != userBadges.end(); ++it)
    {
        const auto entry = it.value().toObject();
        const auto badgeID = entry["badgeId"].toString();
        const auto when = static_cast<qint64>(entry["lastConfirmed"].toDouble());
        const auto json = this->badgeJson_.find(badgeID);
        if (when < oldest || json == this->badgeJson_.end())
        {
            continue;
        }
        registry->registerBadge(json->second);
        registry->assignBadgeToUser(badgeID, UserId{it.key()});
        this->twitchBadges_[it.key()] = {badgeID, when};
    }

    const auto fresh = nowSeconds() - LOOKUP_AFTER_S;
    const auto lookups = root["lookups"].toObject();
    for (auto it = lookups.begin(); it != lookups.end(); ++it)
    {
        const auto when = static_cast<qint64>(it.value().toDouble());
        if (when >= fresh)
        {
            this->lookupTimes_[it.key()] = when;
        }
    }

    qCDebug(chatterinoSeventv)
        << "Restored" << this->knownPaints_.size() << "7TV paints and"
        << this->twitchPaintMap_.size() + this->kickPaintMap_.size()
        << "assignments from disk";
}

void SeventvPaints::queueSave()
{
    this->dirty_ = true;
    if (this->saveQueued_.exchange(true))
    {
        return;
    }
    postToThread([this] {
        QTimer::singleShot(SAVE_DELAY_MS, [this] {
            this->saveQueued_ = false;
            this->save();
        });
    });
}

void SeventvPaints::save()
{
    if (this->file_.isEmpty())
    {
        return;
    }

    QJsonObject root;
    {
        std::shared_lock lock(this->mutex_);
        this->dirty_ = false;

        QJsonObject paints;
        auto users = [&](const auto &map, const auto &confirmed) {
            QJsonObject out;
            for (const auto &[name, paint] : map)
            {
                auto json = this->paintJson_.find(paint->id);
                auto when = confirmed.find(name);
                if (json == this->paintJson_.end() || when == confirmed.end())
                {
                    continue;
                }
                paints[paint->id] = json->second;
                out[name] = QJsonObject{
                    {QStringLiteral("paintId"), paint->id},
                    {QStringLiteral("lastConfirmed"), static_cast<double>(when->second)},
                };
            }
            return out;
        };
        root[QStringLiteral("twitch")] =
            users(this->twitchPaintMap_, this->twitchConfirmed_);
        root[QStringLiteral("kick")] = users(this->kickPaintMap_, this->kickConfirmed_);
        root[QStringLiteral("paints")] = paints;

        QJsonObject badges, userBadges, lookups;
        for (const auto &[userID, entry] : this->twitchBadges_)
        {
            auto json = this->badgeJson_.find(entry.first);
            if (json == this->badgeJson_.end())
            {
                continue;
            }
            badges[entry.first] = json->second;
            userBadges[userID] = QJsonObject{
                {QStringLiteral("badgeId"), entry.first},
                {QStringLiteral("lastConfirmed"), static_cast<double>(entry.second)},
            };
        }
        const auto fresh = nowSeconds() - LOOKUP_AFTER_S;
        for (const auto &[login, when] : this->lookupTimes_)
        {
            if (when >= fresh)
            {
                lookups[login] = static_cast<double>(when);
            }
        }
        root[QStringLiteral("badges")] = badges;
        root[QStringLiteral("twitchBadges")] = userBadges;
        root[QStringLiteral("lookups")] = lookups;
        root[QStringLiteral("version")] = 1;
        root[QStringLiteral("savedAt")] = static_cast<double>(nowSeconds());
    }

    QSaveFile file(this->file_);
    if (!file.open(QFile::WriteOnly) ||
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 ||
        !file.commit())
    {
        qCWarning(chatterinoSeventv)
            << "Couldn't save 7TV paints to" << this->file_;
    }
}

void SeventvPaints::noteTwitchChatter(const QString &login,
                                      const QString &userID)
{
    if (login.isEmpty() || userID.isEmpty() ||
        (!getSettings()->displaySevenTVPaints &&
         !getSettings()->showBadgesSevenTV))
    {
        return;
    }
    this->ensureLoaded();
    this->queueLookup(login, userID);
}

void SeventvPaints::queueLookup(const QString &login, const QString &userID)
{
    const auto key = login.toLower();
    if (key.isEmpty() || userID.isEmpty())
    {
        return;
    }

    {
        std::shared_lock lock(this->mutex_);
        auto it = this->lookupTimes_.find(key);
        if (it != this->lookupTimes_.end() &&
            nowSeconds() - it->second < LOOKUP_AFTER_S)
        {
            return;
        }
    }

    std::unique_lock lock(this->lookupMutex_);
    if (!this->lookedUp_.insert(key).second ||
        this->lookupQueue_.size() >= LOOKUP_QUEUE_MAX)
    {
        return;
    }
    this->lookupQueue_.emplace_back(key, userID);
    lock.unlock();
    this->armLookup();
}

void SeventvPaints::armLookup()
{
    {
        std::unique_lock lock(this->lookupMutex_);
        if (this->lookupArmed_ || this->lookupQueue_.empty())
        {
            return;
        }
        this->lookupArmed_ = true;
    }
    postToThread([this] {
        QTimer::singleShot(LOOKUP_INTERVAL_MS, [this] {
            this->lookUpChatters();
        });
    });
}

void SeventvPaints::lookUpChatters()
{
    std::vector<std::pair<QString, QString>> batch;
    {
        std::unique_lock lock(this->lookupMutex_);
        while (!this->lookupQueue_.empty() && batch.size() < LOOKUP_BATCH)
        {
            batch.push_back(std::move(this->lookupQueue_.front()));
            this->lookupQueue_.pop_front();
        }
    }
    if (batch.empty())
    {
        std::unique_lock lock(this->lookupMutex_);
        this->lookupArmed_ = false;
        return;
    }

    QString query = QStringLiteral("query{");
    for (size_t i = 0; i < batch.size(); i++)
    {
        // Twitch user IDs are digits only
        auto id = batch[i].second;
        id.remove(QRegularExpression(QStringLiteral("[^0-9]")));
        query += QStringLiteral("u%1:userByConnection(platform:TWITCH,id:\"%2\")")
                     .arg(i)
                     .arg(id) +
                 LOOKUP_FIELDS + ' ';
    }
    query += '}';

    NetworkRequest(QUrl(LOOKUP_URL), NetworkRequestType::Post)
        .json(QJsonObject{{QStringLiteral("query"), query}})
        // 7TV's GQL doesn't answer POSTs over HTTP/2 from Qt
        .attribute(QNetworkRequest::Http2AllowedAttribute, false)
        .timeout(15000)
        .onSuccess([this, batch](const NetworkResult &result) {
            const auto root = result.parseJson();
            if (root.contains("errors"))
            {
                qCWarning(chatterinoSeventv)
                    << "7TV paint lookup refused:" << root["errors"];
            }
            this->applyLookup(batch, root["data"].toObject());
        })
        .onError([](const NetworkResult &result) {
            qCWarning(chatterinoSeventv)
                << "7TV paint lookup failed:" << result.formatError();
        })
        .finally([this, batch] {
            {
                std::unique_lock lock(this->lookupMutex_);
                this->lookupArmed_ = false;
                // looked up: the next message after LOOKUP_AFTER_S asks again
                // (a new paint or badge shows without a restart)
                for (const auto &entry : batch)
                {
                    this->lookedUp_.erase(entry.first);
                }
            }
            this->armLookup();
        })
        .execute();
}

void SeventvPaints::applyLookup(
    const std::vector<std::pair<QString, QString>> &batch,
    const QJsonObject &data)
{
    for (size_t i = 0; i < batch.size(); i++)
    {
        const auto &[login, userID] = batch[i];
        const auto user = data[QStringLiteral("u%1").arg(i)].toObject();
        if (user.isEmpty())
        {
            continue;  // 7TV didn't answer for this one
        }
        const std::vector<seventv::eventapi::User> users{
            seventv::eventapi::TwitchUser(QJsonObject{
                {QStringLiteral("id"), userID},
                {QStringLiteral("username"), login},
            }),
        };

        {
            std::unique_lock lock(this->mutex_);
            this->lookupTimes_[login] = nowSeconds();
        }
        const auto style = user["style"].toObject();
        this->applyBadge(userID, style["badge"].toObject());

        const auto paint = style["paint"].toObject();
        const auto paintID = paint["id"].toString();
        if (paintID.isEmpty())
        {
            // no paint (any more): drop a remembered one
            if (auto old = this->getPaint(login, false))
            {
                this->clearPaintFromUsers(old->id, users);
            }
        }
        else
        {
            this->addPaint(paint);
            this->assignPaintToUsers(paintID, users);
        }
    }
    this->queueSave();
}

void SeventvPaints::applyBadge(const QString &userID, const QJsonObject &badge)
{
    auto *registry = getApp()->getSeventvBadges();
    const auto badgeID = badge["id"].toString();
    std::unique_lock lock(this->mutex_);
    if (badgeID.isEmpty())
    {
        // no badge (any more): drop one this lookup gave earlier
        auto it = this->twitchBadges_.find(userID);
        if (it != this->twitchBadges_.end())
        {
            registry->clearBadgeFromUser(it->second.first, UserId{userID});
            this->twitchBadges_.erase(it);
        }
        return;
    }
    registry->registerBadge(badge);
    registry->assignBadgeToUser(badgeID, UserId{userID});
    this->badgeJson_[badgeID] = badge;
    this->twitchBadges_[userID] = {badgeID, nowSeconds()};
}

std::shared_ptr<Paint> SeventvPaints::getPaint(const QString &userName,
                                               bool kick) const
{
    this->ensureLoaded();
    std::shared_lock lock(this->mutex_);

    if (kick)
    {
        const auto it = this->kickPaintMap_.find(userName);
        if (it != this->kickPaintMap_.end())
        {
            return it->second;
        }
    }
    else
    {
        const auto it = this->twitchPaintMap_.find(userName);
        if (it != this->twitchPaintMap_.end())
        {
            return it->second;
        }
    }
    return nullptr;
}

void SeventvPaints::addPaint(const QJsonObject &paintJson)
{
    this->ensureLoaded();
    const auto paintID = paintJson["id"].toString();

    std::unique_lock lock(this->mutex_);

    if (this->knownPaints_.contains(paintID))
    {
        return;
    }

    std::optional<std::shared_ptr<Paint>> paint = parsePaint(paintJson);
    if (!paint)
    {
        return;
    }

    DebugCount::increase(DebugObject::SeventvPaints);
    this->knownPaints_[paintID] = *paint;
    this->paintJson_[paintID] = paintJson;
}

void SeventvPaints::assignPaintToUsers(
    const QString &paintID, std::span<const seventv::eventapi::User> users)
{
    this->ensureLoaded();
    std::unique_lock lock(this->mutex_);

    const auto paintIt = this->knownPaints_.find(paintID);
    if (paintIt == this->knownPaints_.end())
    {
        return;
    }

    bool changed = false;
    int64_t nAdded = 0;
    const auto now = nowSeconds();
    auto addToMap = [&](auto &map, const QString &username) {
        auto it = map.find(username);
        if (it == map.end())
        {
            map.emplace(username, paintIt->second);
            changed = true;
            nAdded++;
        }
        else if (it->second != paintIt->second)
        {
            it->second = paintIt->second;
            changed = true;
        }
    };
    for (const auto &user : users)
    {
        std::visit(variant::Overloaded{
                       [&](const seventv::eventapi::TwitchUser &u) {
                           addToMap(this->twitchPaintMap_, u.userName);
                           this->twitchConfirmed_[u.userName] = now;
                       },
                       [&](const seventv::eventapi::KickUser &u) {
                           addToMap(this->kickPaintMap_, u.userName);
                           this->kickConfirmed_[u.userName] = now;
                       },
                   },
                   user);
    }

    if (nAdded > 0)
    {
        DebugCount::increase(DebugObject::SeventvPaintAssignments, nAdded);
    }

    if (changed)
    {
        postToThread([] {
            getApp()->getWindows()->invalidateChannelViewBuffers();
        });
    }
    lock.unlock();
    this->queueSave();
}

void SeventvPaints::clearPaintFromUsers(
    const QString &paintID, std::span<const seventv::eventapi::User> users)
{
    this->ensureLoaded();
    std::unique_lock lock(this->mutex_);

    int64_t nRemoved = 0;
    auto removeFromMap = [&](auto &map, auto &confirmed,
                             const QString &username) {
        const auto it = map.find(username);
        if (it != map.end() && it->second->id == paintID)
        {
            map.erase(it);
            confirmed.erase(username);
            nRemoved++;
        }
    };
    for (const auto &user : users)
    {
        std::visit(variant::Overloaded{
                       [&](const seventv::eventapi::TwitchUser &u) {
                           removeFromMap(this->twitchPaintMap_,
                                         this->twitchConfirmed_, u.userName);
                       },
                       [&](const seventv::eventapi::KickUser &u) {
                           removeFromMap(this->kickPaintMap_,
                                         this->kickConfirmed_, u.userName);
                       },
                   },
                   user);
    }

    if (nRemoved > 0)
    {
        DebugCount::decrease(DebugObject::SeventvPaintAssignments, nRemoved);
        postToThread([] {
            getApp()->getWindows()->invalidateChannelViewBuffers();
        });
        lock.unlock();
        this->queueSave();
    }
}

}  // namespace chatterino
