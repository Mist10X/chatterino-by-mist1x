#include "providers/seventv/SeventvPersonalLookup.hpp"

#include "Application.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/seventv/SeventvPersonalEmotes.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/PostToThread.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <array>

namespace chatterino {

namespace {

/// a remembered set is trusted this long, then asked again
constexpr qint64 SET_DAYS = 7;
/// "no personal set" is trusted this long
constexpr qint64 NONE_HOURS = 24;
/// one lookup this often; a busy chat makes the queue wait, not 7TV
constexpr int PACE_MS = 350;
/// chatters waiting longer than this are dropped (asked when they speak again)
constexpr size_t QUEUE_LIMIT = 300;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool fresh(const QString &emoteSetID, qint64 at)
{
    const auto age = nowMs() - at;
    return emoteSetID.isEmpty() ? age < NONE_HOURS * 3600 * 1000
                                : age < SET_DAYS * 24 * 3600 * 1000;
}

}  // namespace

SeventvPersonalLookup &SeventvPersonalLookup::instance()
{
    static auto *lookup = new SeventvPersonalLookup;
    return *lookup;
}

SeventvPersonalLookup::SeventvPersonalLookup()
    : pace_(new QTimer(this))
    , saveTimer_(new QTimer(this))
{
    this->pace_->setInterval(PACE_MS);
    QObject::connect(this->pace_, &QTimer::timeout, this, [this] {
        this->lookUpNext();
    });
    this->saveTimer_->setSingleShot(true);
    this->saveTimer_->setInterval(30 * 1000);
    QObject::connect(this->saveTimer_, &QTimer::timeout, this, [this] {
        this->save();
    });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (this->saveTimer_->isActive())
        {
            this->save();
        }
    });
}

void SeventvPersonalLookup::setStorePath(const QString &path)
{
    this->path_ = path;
    this->loaded_ = false;
    this->known_.clear();
    this->asked_.clear();
    this->queue_.clear();
    this->load();
}

QString SeventvPersonalLookup::rememberedSet(const QString &userID) const
{
    return this->known_.value(userID).emoteSetID;
}

int SeventvPersonalLookup::queued() const
{
    return int(this->queue_.size());
}

void SeventvPersonalLookup::load()
{
    if (this->loaded_)
    {
        return;
    }
    this->loaded_ = true;
    if (this->path_.isEmpty())
    {
        auto *app = tryGetApp();
        if (!app)
        {
            return;
        }
        this->path_ = QDir(app->getPaths().cacheDirectory())
                          .filePath(QStringLiteral("seventv-personal.json"));
    }
    QFile file(this->path_);
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    const auto users = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = users.begin(); it != users.end(); ++it)
    {
        const auto entry = it.value().toObject();
        Known known{entry["set"].toString(),
                    qint64(entry["at"].toDouble())};
        if (fresh(known.emoteSetID, known.at))
        {
            this->known_.insert(it.key(), known);
        }
    }
}

void SeventvPersonalLookup::scheduleSave()
{
    if (!this->saveTimer_->isActive())
    {
        this->saveTimer_->start();
    }
}

void SeventvPersonalLookup::save()
{
    if (this->path_.isEmpty())
    {
        return;
    }
    QJsonObject users;
    for (auto it = this->known_.begin(); it != this->known_.end(); ++it)
    {
        if (fresh(it->emoteSetID, it->at))
        {
            users.insert(it.key(), QJsonObject{{"set", it->emoteSetID},
                                               {"at", double(it->at)}});
        }
    }
    QSaveFile file(this->path_);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(users).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void SeventvPersonalLookup::store(const QString &userID,
                                  const QString &emoteSetID)
{
    this->load();
    this->known_.insert(userID, Known{emoteSetID, nowMs()});
    this->scheduleSave();
}

void SeventvPersonalLookup::remember(const QString &userID,
                                     const QString &emoteSetID)
{
    postToThread(
        [this, userID, emoteSetID] {
            this->asked_.insert(userID);
            this->store(userID, emoteSetID);
        },
        this);
}

void SeventvPersonalLookup::noteChatter(const QString &userID,
                                        const QString &login)
{
    if (userID.isEmpty() || this->asked_.contains(userID) ||
        !getSettings()->enableSevenTVPersonalEmotes)
    {
        return;
    }
    auto *app = tryGetApp();
    if (app && !app->getSeventvPersonalEmotes()
                    ->getEmoteSetsForTwitchUser(userID)
                    .isEmpty())
    {
        return;
    }
    this->load();
    this->asked_.insert(userID);

    const auto known = this->known_.find(userID);
    if (known != this->known_.end())
    {
        if (!known->emoteSetID.isEmpty())
        {
            this->loadSet(userID, login, known->emoteSetID);
        }
        return;
    }

    this->queue_.emplace_back(userID, login);
    while (this->queue_.size() > QUEUE_LIMIT)
    {
        this->asked_.remove(this->queue_.front().first);
        this->queue_.pop_front();
    }
    if (!this->pace_->isActive())
    {
        this->lookUpNext();
        this->pace_->start();
    }
}

void SeventvPersonalLookup::lookUpNext()
{
    if (this->queue_.empty())
    {
        this->pace_->stop();
        return;
    }
    const auto [userID, login] = this->queue_.front();
    this->queue_.pop_front();

    auto *app = tryGetApp();
    auto *api = app ? app->getSeventvAPI() : nullptr;
    if (!api)
    {
        return;
    }
    api->getUserByTwitchID(
        userID,
        [this, userID, login](const QJsonObject &json, const QByteArray &) {
            QString emoteSetID;
            const auto user = json["user"].toObject();
            for (const auto &setJson : user["emote_sets"].toArray())
            {
                const auto set = setJson.toObject();
                if (SeventvEmoteSetFlags(
                        SeventvEmoteSetFlag(set["flags"].toInt()))
                        .has(SeventvEmoteSetFlag::Personal))
                {
                    emoteSetID = set["id"].toString();
                    break;
                }
            }
            this->store(userID, emoteSetID);
            if (!emoteSetID.isEmpty())
            {
                this->loadSet(userID, login, emoteSetID);
            }
        },
        [this, userID](const NetworkResult &result) {
            // 404: not a 7TV user at all
            if (result.status() == std::optional<int>(404))
            {
                this->store(userID, QString());
            }
        });
}

void SeventvPersonalLookup::loadSet(const QString &userID,
                                    const QString &login,
                                    const QString &emoteSetID)
{
    SeventvEmotes::getEmoteSet(
        emoteSetID,
        [userID, login, emoteSetID](EmoteMap &&emoteMap, const QString &) {
            auto *app = tryGetApp();
            if (!app || emoteMap.empty())
            {
                return;
            }
            auto *personal = app->getSeventvPersonalEmotes();
            if (!personal->getEmoteSetsForTwitchUser(userID).isEmpty())
            {
                return;  // the EventAPI was quicker
            }
            personal->addEmoteSetForTwitchUser(emoteSetID, std::move(emoteMap),
                                               userID);
            auto set = personal->getEmoteSetByID(emoteSetID);
            auto *events = app->getSeventvEventAPI();
            if (!set || !events)
            {
                return;
            }
            // the same path as an announced set: the user's last message
            // gets the emotes
            const std::array<seventv::eventapi::User, 1> users{
                seventv::eventapi::TwitchUser(QJsonObject{
                    {"id", userID},
                    {"username", login},
                })};
            events->signals_.personalEmoteSetAdded.invoke({
                .connections = users,
                .emoteSet = *set,
            });
        },
        [emoteSetID](const QString &error) {
            qCDebug(chatterinoSeventv)
                << "Failed to load a looked-up personal emote set"
                << emoteSetID << error;
        });
}

}  // namespace chatterino
