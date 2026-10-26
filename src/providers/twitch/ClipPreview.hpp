// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QString>

#include <memory>

namespace chatterino {

class Image;
using ImagePtr = std::shared_ptr<Image>;

/// What the clip card under a message shows (like Chatterino+).
struct ClipPreviewInfo {
    QString slug;
    QString url;
    QString title;
    QString broadcaster;
    QString creator;
    QString gameName;
    ImagePtr thumbnail;
    int views = 0;
    double durationSeconds = 0;
    QDateTime createdAt;
};

namespace clippreview {

/// The clip slug of a Twitch clip link, or empty for any other link.
/// Accepts twitch.tv/<channel>/clip/<slug> and clips.twitch.tv/<slug>.
QString clipSlug(const QString &url);

/// Loaded clip, or nullptr while unknown, loading or unavailable.
std::shared_ptr<const ClipPreviewInfo> get(const QString &slug);

/// Starts loading once per slug (cached; GUI thread). The chat is laid out
/// again when the card is ready.
void request(const QString &slug);

/// "0:06", "1:02:03".
QString formatDuration(double seconds);

/// "1 просмотр" / "2 views" in the UI language.
QString formatViews(int views);

/// "только что" / "5 min ago" relative to now.
QString formatAge(const QDateTime &createdAt, const QDateTime &now);

/// Test hook: put a ready card into the cache without the network.
void insertForTesting(ClipPreviewInfo info);

}  // namespace clippreview

}  // namespace chatterino
