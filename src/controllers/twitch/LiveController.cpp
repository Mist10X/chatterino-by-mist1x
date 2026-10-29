// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/twitch/LiveController.hpp"

#include "common/QLogging.hpp"
#include "Application.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "singletons/WindowManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/Helpers.hpp"

#include <QDebug>

namespace {

const auto &LOG = chatterinoTwitchLiveController;

}

namespace chatterino {

TwitchLiveController::TwitchLiveController()
{
    QObject::connect(&this->refreshTimer, &QTimer::timeout, [this] {
        this->request();
    });
    this->refreshTimer.start(TwitchLiveController::REFRESH_INTERVAL);

    QObject::connect(&this->immediateRequestTimer, &QTimer::timeout, [this] {
        QStringList channelIDs;

        {
            std::unique_lock immediateRequestsLock(
                this->immediateRequestsMutex);
            for (const auto &channelID : this->immediateRequests)
            {
                channelIDs.append(channelID);
            }
            this->immediateRequests.clear();
        }

        if (channelIDs.isEmpty())
        {
            return;
        }

        this->request(channelIDs);
    });
    this->immediateRequestTimer.start(
        TwitchLiveController::IMMEDIATE_REQUEST_INTERVAL);

    QObject::connect(&this->viewerCountTimer, &QTimer::timeout, [this] {
        this->refreshVisibleViewerCounts();
    });
    this->viewerCountTimer.start(TwitchLiveController::VIEWER_COUNT_INTERVAL);
}

void TwitchLiveController::refreshVisibleViewerCounts()
{
    auto *app = tryGetApp();
    if (app == nullptr || app->getWindows() == nullptr)
    {
        return;
    }
    const auto visible = app->getWindows()->getVisibleChannelNames();
    if (visible.empty())
    {
        return;
    }
    // Live channels that are open on screen, by login.
    QHash<QString, std::weak_ptr<TwitchChannel>> open;
    {
        std::shared_lock lock(this->channelsMutex);
        for (const auto &[id, entry] : this->channels)
        {
            auto channel = entry.ptr.lock();
            if (!channel || !channel->isLive())
            {
                continue;
            }
            const auto login = channel->getName().toLower();
            if (visible.contains(login))
            {
                open.insert(login, channel);
            }
        }
    }
    if (open.isEmpty())
    {
        return;
    }
    QStringList logins = open.keys();
    while (logins.size() > VIEWER_COUNT_BATCH)
    {
        logins.removeLast();
    }
    TwitchGql::getViewerCounts(logins, [open](QHash<QString, unsigned> counts) {
        for (auto it = counts.begin(); it != counts.end(); ++it)
        {
            if (auto channel = open.value(it.key()).lock())
            {
                channel->updateViewerCount(it.value());
            }
        }
    });
}

void TwitchLiveController::add(const std::shared_ptr<TwitchChannel> &newChannel)
{
    assert(newChannel != nullptr);

    const auto channelID = newChannel->roomId();
    assert(!channelID.isEmpty());

    {
        std::unique_lock lock(this->channelsMutex);
        this->channels[channelID] = {.ptr = newChannel, .wasChecked = false};
    }

    {
        std::unique_lock immediateRequestsLock(this->immediateRequestsMutex);
        this->immediateRequests.emplace(channelID);
    }
}

void TwitchLiveController::request(std::optional<QStringList> optChannelIDs)
{
    QStringList channelIDs;

    if (optChannelIDs)
    {
        channelIDs = *optChannelIDs;
    }
    else
    {
        std::shared_lock lock(this->channelsMutex);

        for (const auto &channelList : this->channels)
        {
            channelIDs.append(channelList.first);
        }
    }

    if (channelIDs.isEmpty())
    {
        return;
    }

    auto batches =
        splitListIntoBatches(channelIDs, TwitchLiveController::BATCH_SIZE);

    qCDebug(LOG) << "Make" << batches.size() << "requests";

    for (const auto &batch : batches)
    {
        getHelix()->fetchStreams(
            batch, {},
            [this, batch{batch}](const auto &streams) {
                std::unordered_map<QString, std::optional<HelixStream>> results;

                for (const auto &channelID : batch)
                {
                    results[channelID] = std::nullopt;
                }

                for (const auto &stream : streams)
                {
                    results[stream.userId] = stream;
                }

                QStringList deadChannels;

                {
                    std::shared_lock lock(this->channelsMutex);
                    for (const auto &result : results)
                    {
                        auto it = this->channels.find(result.first);
                        if (it != this->channels.end())
                        {
                            if (auto channel = it->second.ptr.lock(); channel)
                            {
                                channel->updateStreamStatus(
                                    result.second, !it->second.wasChecked);
                                it->second.wasChecked = true;
                            }
                            else
                            {
                                deadChannels.append(result.first);
                            }
                        }
                    }
                }

                if (!deadChannels.isEmpty())
                {
                    std::unique_lock lock(this->channelsMutex);
                    for (const auto &deadChannel : deadChannels)
                    {
                        this->channels.erase(deadChannel);
                    }
                }
            },
            [] {
                qCWarning(LOG) << "Failed stream check request";
            },
            [] {});

        getHelix()->fetchChannels(
            batch,
            [this, batch{batch}](const auto &helixChannels) {
                QStringList deadChannels;

                {
                    std::shared_lock lock(this->channelsMutex);
                    for (const auto &helixChannel : helixChannels)
                    {
                        auto it = this->channels.find(helixChannel.userId);
                        if (it != this->channels.end())
                        {
                            if (auto channel = it->second.ptr.lock(); channel)
                            {
                                channel->updateStreamTitle(helixChannel.title);
                                channel->updateDisplayName(helixChannel.name);
                            }
                            else
                            {
                                deadChannels.append(helixChannel.userId);
                            }
                        }
                    }
                }

                if (!deadChannels.isEmpty())
                {
                    std::unique_lock lock(this->channelsMutex);
                    for (const auto &deadChannel : deadChannels)
                    {
                        this->channels.erase(deadChannel);
                    }
                }
            },
            [] {
                qCWarning(LOG) << "Failed stream check request";
            });
    }
}

}  // namespace chatterino
