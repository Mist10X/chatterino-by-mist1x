// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/homies/HomiesBadges.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "singletons/Paths.hpp"
#include "singletons/WindowManager.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThreadPool>
#include <QStringBuilder>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <tuple>

namespace chatterino {

namespace {

using namespace chatterino::literals;

using BadgeMap = std::unordered_map<QString, int>;
using BadgeStorage = std::vector<EmotePtr>;
constexpr auto HOMIES_USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36";
constexpr auto HOMIES_BADGE_FRAME_LIFETIME = std::chrono::minutes{4};
/// The lists change now and then: fetched again this often.
constexpr auto HOMIES_REFRESH_INTERVAL = std::chrono::hours{1};

struct HomiesSource {
    const char *url;
    /// The last good answer is kept here, so badges show up right at
    /// startup even when the site is slow or down.
    const char *cacheFile;
    bool userCentric;
};
constexpr std::array<HomiesSource, 3> HOMIES_SOURCES{{
    {"https://chatterinohomies.com/api/badges/list", "homies-badges-1.json",
     true},
    {"https://itzalex.github.io/badges", "homies-badges-2.json", false},
    {"https://itzalex.github.io/badges2", "homies-badges-3.json", false},
}};

/// Which lists already came fresh from the site this run.
std::array<std::atomic_bool, 3> HOMIES_FRESH{};

QString homiesCachePath(const HomiesSource &source)
{
    return getApp()->getPaths().cacheFilePath(
        QString::fromLatin1(source.cacheFile));
}

QJsonArray extractBadgesArray(const QByteArray &data,
                              const QString &sourceName)
{
    const auto doc = QJsonDocument::fromJson(data);
    if (doc.isObject() && doc.object().value("badges").isArray())
    {
        return doc.object().value("badges").toArray();
    }
    if (doc.isArray() && !doc.array().isEmpty())
    {
        return doc.array();
    }

    qCWarning(chatterinoApp) << "[Homies] Unexpected payload shape from"
                             << sourceName << "- expected { badges: [...] }"
                             << "or a root array.";
    return {};
}

EmotePtr createBadgeEmote(const QString &sourceTag, const QString &badgeName,
                          const QJsonObject &badgeJson)
{
    const auto image1 = badgeJson.value("image1").toString().trimmed();
    if (image1.isEmpty())
    {
        return nullptr;
    }

    const auto tooltip = badgeJson.value("tooltip").toString().trimmed();
    const auto resolvedName = !badgeName.trimmed().isEmpty()
                                  ? badgeName.trimmed()
                                  : (!tooltip.isEmpty() ? tooltip : u"badge"_s);

    auto makeImage = [](const QString &url, qreal scale) {
        if (url.isEmpty())
        {
            return Image::getEmpty();
        }

        auto image = Image::fromUrl(Url{url}, scale);
        image->setFrameCacheLifetime(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                HOMIES_BADGE_FRAME_LIFETIME));
        return image;
    };

    auto emote = Emote{
        .name = EmoteName{sourceTag % u":" % resolvedName},
        .images =
            ImageSet{
                makeImage(image1, 1),
                makeImage(badgeJson.value("image2").toString().trimmed(), 0.5),
                makeImage(badgeJson.value("image3").toString().trimmed(), 0.25),
            },
        .tooltip = Tooltip{tooltip},
        .homePage = Url{},
        .id = EmoteId{image1},
    };

    return std::make_shared<const Emote>(std::move(emote));
}

void storeUserCentricBadges(const QJsonArray &badges, const QString &sourceTag,
                            BadgeMap &badgeMap, BadgeStorage &emotes)
{
    badgeMap.clear();
    emotes.clear();
    badgeMap.reserve(badges.size());
    emotes.reserve(badges.size());

    for (const auto &badgeValue : badges)
    {
        if (isAppAboutToQuit())
        {
            return;
        }
        const auto badgeJson = badgeValue.toObject();
        const auto userId = badgeJson.value("userId").toString().trimmed();
        if (userId.isEmpty())
        {
            continue;
        }

        auto emote = createBadgeEmote(sourceTag, userId, badgeJson);
        if (!emote)
        {
            continue;
        }

        const int index = static_cast<int>(emotes.size());
        emotes.push_back(std::move(emote));
        badgeMap[userId] = index;
    }
}

void storeBadgeCentricBadges(const QJsonArray &badges, const QString &sourceTag,
                             BadgeMap &badgeMap, BadgeStorage &emotes)
{
    qsizetype userCountEstimate = 0;
    for (const auto &badgeValue : badges)
    {
        userCountEstimate +=
            badgeValue.toObject().value("users").toArray().size();
    }

    badgeMap.clear();
    emotes.clear();
    badgeMap.reserve(static_cast<size_t>(
        std::max<qsizetype>(badges.size(), userCountEstimate)));
    emotes.reserve(badges.size());

    for (const auto &badgeValue : badges)
    {
        if (isAppAboutToQuit())
        {
            return;
        }
        const auto badgeJson = badgeValue.toObject();
        auto emote = createBadgeEmote(
            sourceTag, badgeJson.value("tooltip").toString(), badgeJson);
        if (!emote)
        {
            continue;
        }

        const int index = static_cast<int>(emotes.size());
        emotes.push_back(std::move(emote));

        for (const auto &userValue : badgeJson.value("users").toArray())
        {
            const auto userId = userValue.toString().trimmed();
            if (!userId.isEmpty())
            {
                badgeMap[userId] = index;
            }
        }
    }
}

}  // namespace

HomiesBadges::HomiesBadges()
{
    QTimer::singleShot(3500, [this] {
        this->startLoading();
    });
}

void HomiesBadges::startLoading()
{
    if (this->loadStarted_.exchange(true))
    {
        return;
    }

    this->loadHomiesBadges();

    auto *refresh = new QTimer();  // lives as long as the app
    QObject::connect(refresh, &QTimer::timeout, [this] {
        this->loadHomiesBadges();
    });
    refresh->start(HOMIES_REFRESH_INTERVAL);
}

void HomiesBadges::queueRefresh()
{
    if (this->refreshQueued_.exchange(true))
    {
        return;
    }

    // may be called off the GUI thread: the redraw runs on it
    QTimer::singleShot(250, QCoreApplication::instance(), [this] {
        this->refreshQueued_.store(false);
        if (auto *windows = getApp()->getWindows())
        {
            windows->invalidateChannelViewBuffers();
        }
    });
}

void HomiesBadges::loadHomiesBadges()
{
    // Shows the badges in the parsed answer; false: nothing usable in it.
    const auto apply = [this](size_t index, const QByteArray &data,
                              bool fresh) {
        if (isAppAboutToQuit())
        {
            return false;
        }
        const auto &source = HOMIES_SOURCES.at(index);
        const auto badges =
            extractBadgesArray(data, QString::fromLatin1(source.url));

        BadgeMap badgeMap;
        BadgeStorage emotes;
        if (source.userCentric)
        {
            storeUserCentricBadges(badges, u"homies"_s, badgeMap, emotes);
        }
        else
        {
            storeBadgeCentricBadges(badges, u"homies"_s, badgeMap, emotes);
        }
        const auto loadedCount = emotes.size();
        // a bad answer keeps the old list; nothing more while closing (this
        // runs in the background and the app is being torn down)
        if (loadedCount == 0 || isAppAboutToQuit())
        {
            return false;
        }

        {
            std::unique_lock lock(this->mutex_);
            // the saved copy never replaces a list fresh from the site
            if (!fresh && HOMIES_FRESH.at(index).load())
            {
                return false;
            }
            if (fresh)
            {
                HOMIES_FRESH.at(index).store(true);
            }
            auto [targetMap, targetEmotes] =
                index == 0   ? std::tie(this->badgeMap_, this->emotes_)
                : index == 1 ? std::tie(this->badgeMap2_, this->emotes2_)
                             : std::tie(this->badgeMap3_, this->emotes3_);
            targetMap = std::move(badgeMap);
            targetEmotes = std::move(emotes);
        }
        this->hasLoadedBadges_.store(true);
        this->queueRefresh();

        qCDebug(chatterinoApp) << "[Homies] Loaded" << loadedCount
                               << "badges from" << source.url;
        return true;
    };

    // First run: what was saved last time, until the fresh list is in (read
    // off the GUI thread, the main list is a few MB of JSON).
    if (!this->hasLoadedBadges_.load())
    {
        QThreadPool::globalInstance()->start([apply] {
            for (size_t index = 0; index < HOMIES_SOURCES.size(); ++index)
            {
                if (isAppAboutToQuit())
                {
                    return;
                }
                QFile cached(homiesCachePath(HOMIES_SOURCES.at(index)));
                if (!HOMIES_FRESH.at(index).load() &&
                    cached.open(QIODevice::ReadOnly))
                {
                    apply(index, cached.readAll(), false);
                }
            }
        });
    }

    for (size_t index = 0; index < HOMIES_SOURCES.size(); ++index)
    {
        const auto &source = HOMIES_SOURCES.at(index);

        NetworkRequest(QString::fromLatin1(source.url))
            .header("Accept", "application/json")
            .header("User-Agent", HOMIES_USER_AGENT)
            // the main list is ~0.6 MB compressed: 5 s often was not enough
            .timeout(60000)
            .concurrent()
            .onSuccess([apply, index](const NetworkResult &result) {
                if (isAppAboutToQuit() || !apply(index, result.getData(), true))
                {
                    return;
                }
                QSaveFile file(homiesCachePath(HOMIES_SOURCES.at(index)));
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(result.getData());
                    file.commit();
                }
            })
            .onError([url = source.url](const NetworkResult &result) {
                qCWarning(chatterinoApp) << "[Homies] Failed to load" << url
                                         << ":" << result.formatError();
            })
            .execute();
    }

}


std::array<EmotePtr, 3> HomiesBadges::getBadges(const QString &userId) const
{
    if (!this->hasLoadedBadges_.load())
    {
        return {};
    }

    std::shared_lock lock(this->mutex_);
    return {
        this->lookupBadge(this->badgeMap_, this->emotes_, userId),
        this->lookupBadge(this->badgeMap2_, this->emotes2_, userId),
        this->lookupBadge(this->badgeMap3_, this->emotes3_, userId),
    };
}

EmotePtr HomiesBadges::lookupBadge(const BadgeMap &badgeMap,
                                   const BadgeStorage &emotes,
                                   const QString &userId) const
{
    const auto it = badgeMap.find(userId);
    if (it != badgeMap.end() && it->second >= 0 &&
        it->second < static_cast<int>(emotes.size()))
    {
        return emotes.at(it->second);
    }

    return nullptr;
}

}  // namespace chatterino
