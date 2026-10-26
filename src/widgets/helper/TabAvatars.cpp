// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/TabAvatars.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "singletons/Paths.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>

namespace chatterino {

using namespace std::chrono_literals;

namespace {

/// A saved avatar younger than this is used without asking Twitch again.
constexpr auto FRESH_FOR = std::chrono::days(7);
/// Helix takes up to 100 logins per request.
constexpr qsizetype LOGINS_PER_REQUEST = 100;

constexpr QStringView KICK_PREFIX = u"kick:";

QString avatarFile(const QString &key)
{
    const auto &twitch = getApp()->getPaths().twitchProfileAvatars;
    if (key.startsWith(KICK_PREFIX))
    {
        QDir kick(twitch + "/../kick");
        kick.mkpath(".");
        return kick.absoluteFilePath(key.mid(KICK_PREFIX.size()) + ".png");
    }
    // The same file the channel notifications use (Toasts.cpp).
    return twitch + '/' + key + ".png";
}

bool isFresh(const QFileInfo &file)
{
    auto age = std::chrono::milliseconds(
        file.lastModified().msecsTo(QDateTime::currentDateTime()));
    return age < FRESH_FOR;
}

QPixmap roundCrop(const QPixmap &source, int size)
{
    QImage image = source.toImage().scaled(size, size,
                                           Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
    QImage out(size, size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.drawImage(QPoint((size - image.width()) / 2,
                             (size - image.height()) / 2),
                      image);
    painter.end();

    return QPixmap::fromImage(out);
}

}  // namespace

TabAvatars &TabAvatars::instance()
{
    static auto *avatars = new TabAvatars;
    return *avatars;
}

QPixmap TabAvatars::circle(const QString &login, int size)
{
    if (login.isEmpty() || size <= 0)
    {
        return {};
    }

    auto it = this->entries_.find(login);
    if (it == this->entries_.end())
    {
        it = this->entries_.insert(login, {});

        QFileInfo saved(avatarFile(login));
        if (saved.isFile() && it->pixmap.load(saved.filePath()))
        {
            it->state = State::Ready;
        }
        if (it->state != State::Ready || !isFresh(saved))
        {
            this->queueLookup(login);
        }
    }

    if (it->state != State::Ready)
    {
        return {};
    }

    auto key = login + '@' + QString::number(size);
    auto circle = this->circles_.constFind(key);
    if (circle != this->circles_.constEnd())
    {
        return *circle;
    }
    return *this->circles_.insert(key, roundCrop(it->pixmap, size));
}

void TabAvatars::queueLookup(const QString &login)
{
    if (login.startsWith(KICK_PREFIX))
    {
        this->lookUpKick(login);
        return;
    }

    this->queued_.append(login);
    if (this->queued_.size() == 1)
    {
        // Tabs are created one after another at start; ask for all at once.
        QTimer::singleShot(250ms, this, [this] {
            this->lookUpQueued();
        });
    }
}

void TabAvatars::lookUpQueued()
{
    auto logins = std::exchange(this->queued_, {});
    for (qsizetype from = 0; from < logins.size(); from += LOGINS_PER_REQUEST)
    {
        auto batch = logins.mid(from, LOGINS_PER_REQUEST);
        getHelix()->fetchUsers(
            {}, batch,
            [this, batch](const std::vector<HelixUser> &users) {
                QStringList unanswered = batch;
                for (const auto &user : users)
                {
                    auto login = user.login.toLower();
                    unanswered.removeAll(login);
                    if (user.profileImageUrl.isEmpty())
                    {
                        this->fail(login);
                    }
                    else
                    {
                        this->download(login, user.profileImageUrl);
                    }
                }
                for (const auto &login : unanswered)
                {
                    this->fail(login);
                }
            },
            [this, batch] {
                qCWarning(chatterinoWidget)
                    << "Tab avatars: Twitch didn't return users" << batch;
                for (const auto &login : batch)
                {
                    this->fail(login);
                }
            });
    }
}

void TabAvatars::lookUpKick(const QString &key)
{
    auto slug = key.mid(KICK_PREFIX.size());
    // As the user card does: the channel's own picture, else Kick's default.
    KickApi::privateChannelInfo(slug, [this, key, slug](const auto &res) {
        if (res && res->user.profilePictureURL)
        {
            this->download(key, *res->user.profilePictureURL);
            return;
        }
        KickApi::privateChannelInfoSmall(slug, [this, key](const auto &small) {
            if (small && small->user.profilePictureURL)
            {
                this->download(key, *small->user.profilePictureURL);
            }
            else
            {
                this->fail(key);
            }
        });
    });
}

void TabAvatars::download(const QString &login, const QString &url)
{
    NetworkRequest(QUrl(url))
        .caller(this)
        .onSuccess([this, login](const NetworkResult &result) {
            this->finish(login, result.getData());
        })
        .onError([this, login](const NetworkResult &result) {
            qCWarning(chatterinoWidget)
                << "Tab avatars: download failed for" << login
                << result.formatError();
            this->fail(login);
        })
        .execute();
}

void TabAvatars::finish(const QString &login, const QByteArray &data)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(data))
    {
        this->fail(login);
        return;
    }

    QFile file(avatarFile(login));
    if (file.open(QFile::WriteOnly | QFile::Truncate))
    {
        file.write(data);
    }

    auto &entry = this->entries_[login];
    entry.state = State::Ready;
    entry.pixmap = pixmap;
    this->circles_.removeIf([&login](const auto &it) {
        return it.key().startsWith(login + '@');
    });
    this->loaded(login);
}

void TabAvatars::fail(const QString &login)
{
    auto &entry = this->entries_[login];
    if (entry.state != State::Ready)
    {
        // An older saved avatar stays in use; without one, the tab keeps its
        // letter until the next start.
        entry.state = State::Failed;
    }
}

}  // namespace chatterino
