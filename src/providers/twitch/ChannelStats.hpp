// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Emote.hpp"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

namespace chatterino {

class Channel;

/// What happened in one channel's chat during the current stream (or, while
/// it is offline, since the channel was opened), for the "Chat statistics"
/// window. The part of a stream before the chat was opened is read from the
/// public rustlog mirrors.
struct ChannelStatsData {
    struct Chatter {
        QString name;
        QColor color;
        int messages = 0;
    };
    struct EmoteUse {
        EmotePtr emote;
        int uses = 0;
    };
    /// Reading the beginning of the stream from the public logs.
    enum class History {
        NotNeeded,
        Loading,
        Done,
        /// some stretches could not be read
        Partial,
        /// the channel is not in the logs (or opted out of them)
        Unavailable,
    };

    QDateTime since;
    bool sinceStreamStart = false;
    bool live = false;
    int messages = 0;
    int subscriptions = 0;
    int moderation = 0;
    QHash<QString, Chatter> chatters;  // login -> chatter
    QHash<QString, EmoteUse> emotes;   // emote name -> uses
    int firstMessages = 0;             // first messages in the channel
    QStringList newChatters;           // their logins (the first 500)
    QMap<qint64, int> perMinute;       // minute since epoch -> messages

    History history = History::NotNeeded;
    /// The logs fill [since, historyUntil); read up to historyRead so far.
    QDateTime historyUntil;
    QDateTime historyRead;
};

class ChannelStats
{
public:
    /// Starts counting this channel (once per channel object); the messages
    /// already in it are counted too.
    static void attach(const std::shared_ptr<Channel> &channel);
    /// A copy of the numbers so far (empty if the channel is not counted).
    static ChannelStatsData get(const QString &channelName);
};

}  // namespace chatterino
