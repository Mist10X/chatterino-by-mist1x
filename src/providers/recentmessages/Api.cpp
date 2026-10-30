// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/recentmessages/Api.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "providers/recentmessages/Impl.hpp"
#include "util/PostToThread.hpp"

namespace {

const auto &LOG = chatterinoRecentMessages;

}

namespace chatterino::recentmessages {

using namespace recentmessages::detail;

namespace {

/// A failed load is tried once more after a short pause: right after the
/// start every channel asks at once, and one of them may time out (408).
constexpr int RETRIES = 1;
constexpr int RETRY_DELAY_MS = 4000;

void loadAttempt(
    const QString &channelName, std::weak_ptr<Channel> channelPtr,
    ResultCallback onLoaded, ErrorCallback onError, const int limit,
    const std::optional<std::chrono::time_point<std::chrono::system_clock>>
        after,
    const std::optional<std::chrono::time_point<std::chrono::system_clock>>
        before,
    const bool jitter, const int attempt)
{
    qCDebug(LOG) << "Loading recent messages for" << channelName;

    const auto url =
        constructRecentMessagesUrl(channelName, limit, after, before);

    const long delayMs = jitter ? std::rand() % 100 : 0;
    QTimer::singleShot(delayMs, [=] {
        if (isAppAboutToQuit())
        {
            return;
        }

        NetworkRequest(url)
            .onSuccess([channelPtr, onLoaded](const auto &result) {
                assert(!isAppAboutToQuit());

                auto shared = channelPtr.lock();
                if (!shared)
                {
                    return;
                }

                qCDebug(LOG) << "Successfully loaded recent messages for"
                             << shared->getName();

                auto root = result.parseJson();
                auto parsedMessages = parseRecentMessages(root);

                auto builtMessages =
                    buildRecentMessages(parsedMessages, shared.get());

                postToThread(
                    [shared = std::move(shared), root = std::move(root),
                     messages = std::move(builtMessages), onLoaded]() mutable {
                        assert(!isAppAboutToQuit());

                        const auto errorCode =
                            root.value("error_code").toString();
                        if (!errorCode.isEmpty())
                        {
                            qCDebug(LOG)
                                << QString("Got error from API: error_code=%1, "
                                           "channel=%2")
                                       .arg(errorCode, shared->getName());
                            if (errorCode == "channel_not_joined" &&
                                !messages.empty())
                            {
                                shared->addSystemMessage(
                                    "Message history service recovering, there "
                                    "may "
                                    "be gaps in the message history.");
                            }
                        }

                        onLoaded(messages);
                    });
            })
            .onError([=](const NetworkResult &result) {
                auto shared = channelPtr.lock();
                if (!shared)
                {
                    return;
                }
                assert(!isAppAboutToQuit());

                if (attempt < RETRIES)
                {
                    QTimer::singleShot(RETRY_DELAY_MS, [=] {
                        if (!isAppAboutToQuit() && !channelPtr.expired())
                        {
                            loadAttempt(channelName, channelPtr, onLoaded,
                                        onError, limit, after, before, false,
                                        attempt + 1);
                        }
                    });
                    return;
                }

                qCDebug(LOG) << "Failed to load recent messages for"
                             << shared->getName();

                shared->addSystemMessage(
                    QStringLiteral(
                        "Message history service unavailable (Error: %1)")
                        .arg(result.formatError()));

                onError();
            })
            .execute();
    });
}

}  // namespace

void load(
    const QString &channelName, std::weak_ptr<Channel> channelPtr,
    ResultCallback onLoaded, ErrorCallback onError, const int limit,
    const std::optional<std::chrono::time_point<std::chrono::system_clock>>
        after,
    const std::optional<std::chrono::time_point<std::chrono::system_clock>>
        before,
    const bool jitter)
{
    loadAttempt(channelName, std::move(channelPtr), std::move(onLoaded),
                std::move(onError), limit, after, before, jitter, 0);
}


}  // namespace chatterino::recentmessages
