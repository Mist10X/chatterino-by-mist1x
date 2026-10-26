#pragma once

#include "providers/seventv/paints/Paint.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace chatterino {

namespace seventv::eventapi {
struct TwitchUser;
struct KickUser;
using User = std::variant<TwitchUser, KickUser>;
}  // namespace seventv::eventapi

/// 7TV paints and who wears them.
///
/// Both are also kept on disk (Misc/seventv-paints.json), so names are
/// painted right after a start instead of only once 7TV sends each user's
/// entitlement again. 7TV's live updates still win; an assignment that 7TV
/// hasn't confirmed for two weeks is dropped.
///
/// 7TV only pushes a paint when the wearer's own client reports being in the
/// channel, so chatters whose paint isn't known (or is older than six hours)
/// are also looked up: at most one request per second, up to 10 chatters
/// each, every chatter at most once per run (like Chatterino+).
///
/// The same lookup brings the chatter's 7TV badge; badges learnt this way are
/// kept on disk too and are in the badge registry before the first message is
/// built, so a known badge (your own included) shows right after a start.
class SeventvPaints
{
public:
    SeventvPaints();
    ~SeventvPaints();

    void addPaint(const QJsonObject &paintJson);
    void assignPaintToUsers(const QString &paintID,
                            std::span<const seventv::eventapi::User> users);
    void clearPaintFromUsers(const QString &paintID,
                             std::span<const seventv::eventapi::User> users);

    std::shared_ptr<Paint> getPaint(const QString &userName, bool kick) const;

    /// A Twitch chatter was seen; look their paint up if it isn't known.
    void noteTwitchChatter(const QString &login, const QString &userID);

    /// Reads what was saved on disk (once). Message builders call it before
    /// asking for 7TV badges.
    void ensureLoaded() const;

private:
    void load();
    void queueLookup(const QString &login, const QString &userID);
    void applyBadge(const QString &userID, const QJsonObject &badge);
    void save();
    void queueSave();
    void armLookup();
    void lookUpChatters();
    void applyLookup(const std::vector<std::pair<QString, QString>> &batch,
                     const QJsonObject &data);

    std::mutex lookupMutex_;
    std::unordered_set<QString> lookedUp_;
    std::deque<std::pair<QString, QString>> lookupQueue_;
    bool lookupArmed_ = false;

    mutable std::once_flag loaded_;
    QString file_;
    std::atomic<bool> dirty_{false};
    std::atomic<bool> saveQueued_{false};

    /// Raw 7TV paint definitions, as they are written to disk.
    std::unordered_map<QString, QJsonObject> paintJson_;
    /// When 7TV last confirmed a user's paint (seconds since epoch).
    std::unordered_map<QString, qint64> twitchConfirmed_;
    std::unordered_map<QString, qint64> kickConfirmed_;
    /// 7TV badges learnt by lookups: definitions, and Twitch user ID => (badge
    /// ID, when 7TV confirmed it).
    std::unordered_map<QString, QJsonObject> badgeJson_;
    std::unordered_map<QString, std::pair<QString, qint64>> twitchBadges_;
    /// Login => when 7TV was last asked about this chatter.
    std::unordered_map<QString, qint64> lookupTimes_;

    mutable std::shared_mutex mutex_;

    std::unordered_map<QString, std::shared_ptr<Paint>> kickPaintMap_;

    std::unordered_map<QString, std::shared_ptr<Paint>> twitchPaintMap_;

    std::unordered_map<QString, std::shared_ptr<Paint>> knownPaints_;
};

}  // namespace chatterino
