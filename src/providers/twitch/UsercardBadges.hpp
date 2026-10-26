#pragma once

#include "messages/Emote.hpp"
#include "providers/twitch/api/TwitchGql.hpp"

#include <functional>
#include <vector>

class QObject;

namespace chatterino {

// Read-only earned badge collection, scoped to the actual Twitch identity and
// channel. This public Twitch query needs neither a token nor PLUS.
void requestUsercardBadges(
    const QString &userId, const QString &login, const QString &channel,
    QObject *context, std::function<void(QVector<GqlBadge>)> success,
    std::function<void(const QString &)> failure);

std::vector<EmotePtr> usercardBadgeEmotes(const QVector<GqlBadge> &badges);

}  // namespace chatterino
