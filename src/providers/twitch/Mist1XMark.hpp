// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringView>

#include <memory>
#include <optional>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

/// Who else uses Chatterino by Mist1X, without any server: messages sent
/// over IRC carry a client-nonce like Twitch's web chat (32 hex digits) whose
/// last 8 digits sign the first 24. Another copy of this build that sees such
/// a message remembers the sender's Twitch ID and shows the build's badge at
/// their name. Nothing leaves the chat itself; anyone could copy the mark, so
/// the badge is a nicety, not a proof.
namespace mist1xmark {

/// A fresh nonce carrying the mark.
QString makeNonce();

/// Whether a client-nonce carries the mark.
bool isMarked(QStringView nonce);

/// The sender of a marked message (kept on disk, 60 days after the last
/// marked message).
void remember(const QString &userID);

bool isKnown(const QString &userID);

/// The badge (the app's icon) for known users, if the setting shows it.
std::optional<EmotePtr> badgeFor(const QString &userID);

/// The online list (test version): this copy says "I use the build" with its
/// Twitch ID (at start, on an account change and once a day; setting
/// mist1xShareId), and fetches the IDs that get the badge every 30 minutes.
/// GUI thread, once.
void startOnline();

/// The users' service (Cloudflare Worker).
QString serviceUrl();

/// Tests only: forget everyone, do not touch the disk.
void resetForTesting();

}  // namespace mist1xmark

}  // namespace chatterino
