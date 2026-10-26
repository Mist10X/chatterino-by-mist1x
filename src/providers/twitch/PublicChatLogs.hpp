// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QObject;

namespace chatterino::publiclogs {

/// A person's older messages in one channel, read straight from the public
/// rustlog mirrors (logs.zonian.dev, logs.twitchmetrics.xyz, logs.spanix.team)
/// by Twitch user ID, so the usercard needs no Archive for them.
///
/// Politeness, as in the Archive: one request at a time per service and 1.5 s
/// between two of them; a 429/503 holds that service back; a person or channel
/// that opted out of logging (HTTP 403) is not asked again this session, on any
/// mirror.
///
/// The answer has the shape of the Archive's "history" page, so the usercard
/// reads both alike: {identity:{user_id,user}, channel, source:"public_logs",
/// status: found|empty|refused|error, rows:[{id,user,user_id,channel,time_utc,
/// text,display_name,color,badges,first_msg}], more, next_cursor, error}.
using PageCallback = std::function<void(const QJsonObject &page)>;

/// One page (newest first). `before` is the previous page's next_cursor
/// ({time_utc,id}) or empty for the newest page. The callback is dropped when
/// `caller` is gone.
void requestPage(const QString &userId, const QString &login,
                 const QString &channel, const QJsonObject &before,
                 const QObject *caller, PageCallback callback);

/// Messages a page returns at most.
constexpr int PAGE_SIZE = 100;

/// One raw IRC line of a mirror's answer as a history row, or nothing when it
/// is not this person's chat message in this channel.
std::optional<QJsonObject> parseLine(const QByteArray &line,
                                     const QString &userId,
                                     const QString &channel);

/// What a 403 body refuses: "user", "channel" or "" (not an opt-out).
QString refusalKind(const QByteArray &body);

/// Tests only: other mirror base URLs and spacing; forgets opt-outs.
void setMirrorsForTesting(const QStringList &mirrors, int spacingMs);

}  // namespace chatterino::publiclogs
