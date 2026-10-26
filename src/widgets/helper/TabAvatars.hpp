// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace chatterino {

/// Channel avatars for notebook tabs.
///
/// A channel is its Twitch login, or "kick:<slug>" for a Kick channel. Each
/// profile image is looked up once (Twitch: Helix, batched; Kick: the channel
/// info the user card uses too) and kept on disk next to the notification
/// avatars, so later starts don't ask again until the copy is a week old.
class TabAvatars : public QObject
{
    Q_OBJECT

public:
    static TabAvatars &instance();

    /// A round avatar `size` pixels across, or a null pixmap while the
    /// channel's avatar isn't known yet (the first call queues a lookup).
    QPixmap circle(const QString &login, int size);

Q_SIGNALS:
    void loaded(const QString &login);

private:
    TabAvatars() = default;

    enum class State : std::uint8_t {
        Loading,
        Ready,
        Failed,
    };

    struct Entry {
        State state = State::Loading;
        QPixmap pixmap;
    };

    void queueLookup(const QString &login);
    void lookUpQueued();
    void lookUpKick(const QString &key);
    void download(const QString &login, const QString &url);
    void finish(const QString &login, const QByteArray &data);
    void fail(const QString &login);

    QHash<QString, Entry> entries_;
    QHash<QString, QPixmap> circles_;
    QStringList queued_;
};

}  // namespace chatterino
