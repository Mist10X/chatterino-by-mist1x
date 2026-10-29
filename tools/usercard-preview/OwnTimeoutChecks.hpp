// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QElapsedTimer>
#include <QLabel>

// The own timeout in the input box (like Chatterino+): red with a countdown,
// green once it is over, normal after the first message. Synthetic channel,
// no IRC / PubSub connection; the state is driven through the same
// TwitchChannel entry points that IRC and PubSub use.

#include "common/UiLanguage.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "widgets/helper/ResizingTextEdit.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitInput.hpp"

#include <QCoreApplication>
#include <cstdio>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace owntimeout {

inline void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

inline QString before(const QString &pattern)
{
    return pattern.section(QStringLiteral("%1"), 0, 0);
}

}  // namespace owntimeout

namespace chatterino {
/// Room modes are set by Twitch's ROOMSTATE; this fixture sets them directly.
class TwitchChannelTestAccess
{
public:
    static void setSlowMode(TwitchChannel &channel, int seconds)
    {
        auto modes = *channel.accessRoomModes();
        modes.slowMode = seconds;
        channel.setRoomModes(modes);
    }
};
}  // namespace chatterino

namespace owntimeout {

inline int runOwnTimeoutChecks(chatterino::Split &split, const QString &report,
                               const QString &screenshotBase)
{
    using namespace chatterino;
    using Kind = TwitchChannel::OwnRestriction::Kind;
    QJsonArray checks;
    int failed = 0;
    auto check = [&](const QString &name, bool ok) {
        failed += ok ? 0 : 1;
        checks.append(QJsonObject{{"name", name}, {"ok", ok}});
        std::fprintf(stderr, "own-timeout check: %s %s\n", ok ? "ok" : "FAIL",
                     name.toUtf8().constData());
    };
    auto shot = [&](const QString &suffix) {
        if (!screenshotBase.isEmpty())
        {
            split.getInput().grab().save(screenshotBase + suffix + ".png",
                                         "PNG");
        }
    };

    auto channel = std::make_shared<TwitchChannel>(QStringLiteral("archive_room"));
    split.setChannel(IndirectChannel(std::static_pointer_cast<Channel>(channel)));
    split.resize(560, 220);
    split.show();
    pump(200);
    ResizingTextEdit *edit = nullptr;
    for (auto *candidate : split.getInput().findChildren<QTextEdit *>())
    {
        if (auto *resizing = dynamic_cast<ResizingTextEdit *>(candidate))
        {
            edit = resizing;
            break;
        }
    }
    if (edit == nullptr)
    {
        check("input box found", false);
        return failed;
    }
    const auto timedOutPrefix = before(uiText("You are timed out · %1 left"));
    const auto endedText = uiText("Your timeout has ended — you can chat again");
    const auto unbannedText =
        uiText("You have been unbanned — you can chat again");

    // 1. timed out (CLEARCHAT with ban-duration / msg_timedout notice)
    // (Typing into the real input needs app services this fixture lacks,
    // so the draft hand-back is not exercised here.)
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(300));
    pump(150);
    check("timed out: the countdown shows, the input stays usable",
          !edit->isReadOnly() && edit->toPlainText().isEmpty() &&
              edit->placeholderText().startsWith(timedOutPrefix) &&
              (edit->placeholderText().contains(QStringLiteral("5:00")) ||
               edit->placeholderText().contains(QStringLiteral("4:59"))));
    shot(QStringLiteral("-timeout"));
    pump(1200);
    check("the countdown ticks every second",
          edit->placeholderText().contains(QStringLiteral("4:59")) ||
              edit->placeholderText().contains(QStringLiteral("4:58")));
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(299));
    check("a repeated notice about the same timeout changes nothing",
          channel->ownRestriction().kind == Kind::TimedOut);

    // 2. removed early (PubSub chatrooms-user-v1 untimeout)
    channel->handleOwnModerationPubSub(
        QJsonObject{{"action", "untimeout"}, {"channel_id", "1"}});
    pump(150);
    check("untimeout: green and editable again",
          channel->ownRestriction().kind == Kind::Lifted &&
              !edit->isReadOnly() && edit->placeholderText() == endedText);
    pump(100);
    shot(QStringLiteral("-ended"));

    // 3. the first message afterwards
    channel->acknowledgeOwnRestrictionLifted();
    pump(100);
    check("after the first message the input is normal again",
          channel->ownRestriction().kind == Kind::None &&
              !edit->isReadOnly() && edit->placeholderText() != endedText);
    shot(QStringLiteral("-normal"));

    // 4. runs out by itself
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(2));
    pump(150);
    const bool wasRed = channel->ownRestriction().kind == Kind::TimedOut &&
                        edit->placeholderText().startsWith(timedOutPrefix);
    pump(3200);
    check("a timeout that runs out turns the input green",
          wasRed && channel->ownRestriction().kind == Kind::Lifted &&
              edit->placeholderText() == endedText && !edit->isReadOnly());
    channel->acknowledgeOwnRestrictionLifted();

    // 5. PubSub timeout with only expires_in_ms, then ban and unban
    channel->handleOwnModerationPubSub(QJsonObject{
        {"action", "timeout"}, {"channel_id", "1"}, {"expires_in_ms", 600000}});
    pump(100);
    check("PubSub timeout starts the countdown",
          channel->ownRestriction().kind == Kind::TimedOut &&
              edit->placeholderText().startsWith(timedOutPrefix) &&
              (edit->placeholderText().contains(QStringLiteral("10:00")) ||
               edit->placeholderText().contains(QStringLiteral("9:59"))));
    channel->handleOwnModerationPubSub(
        QJsonObject{{"action", "ban"}, {"channel_id", "1"}});
    pump(100);
    check("ban: the ban text",
          channel->ownRestriction().kind == Kind::Banned &&
              edit->placeholderText() ==
                  uiText("You are banned in this channel"));
    shot(QStringLiteral("-banned"));
    channel->handleOwnModerationPubSub(
        QJsonObject{{"action", "unban"}, {"channel_id", "1"}});
    pump(100);
    check("unban: green with the unban text",
          channel->ownRestriction().liftedBan &&
              edit->placeholderText() == unbannedText && !edit->isReadOnly());
    channel->acknowledgeOwnRestrictionLifted();

    // 6. each account has its own state
    const QString other = QStringLiteral("4242");
    channel->setOwnBanned(other);
    pump(100);
    check("a ban of another own account leaves this account's input alone",
          channel->ownRestriction().kind == Kind::None &&
              channel->ownRestrictionOf(other).kind == Kind::Banned &&
              !edit->placeholderText().startsWith(
                  uiText("You are banned in this channel")));

    // 7. "X unbanned Y" in chat lifts Y, even when Y is not signed in
    {
        MessageBuilder builder;
        builder->flags.set(MessageFlag::ModerationAction,
                           MessageFlag::Untimeout);
        builder->timeoutUser = QStringLiteral("other_account");
        builder->timeoutUserID = other;
        builder.emplace<TextElement>(QStringLiteral("someone unbanned other"),
                                     MessageElementFlag::Text);
        // Channel::addMessage needs app services this fixture lacks; the
        // hook listens to the signal addMessage raises for each live message.
        MessagePtr message = builder.release();
        channel->messageAppended.invoke(message, std::nullopt);
    }
    pump(100);
    check("an unban message in chat lifts that account",
          channel->ownRestrictionOf(other).kind == Kind::Lifted &&
              channel->ownRestrictionOf(other).liftedBan);

    // 8. a send that Twitch accepts (no "timed out" notice) ends it
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(300));
    pump(100);
    channel->noteSendWhileRestricted();
    pump(5600);
    check("a message accepted by Twitch ends a missed timeout",
          channel->ownRestriction().kind == Kind::Lifted);
    channel->acknowledgeOwnRestrictionLifted();
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(300));
    pump(100);
    channel->noteSendWhileRestricted();
    pump(1000);
    // Twitch answers "You are timed out for N more seconds."
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(298));
    pump(4600);
    check("a rejected message keeps the timeout",
          channel->ownRestriction().kind == Kind::TimedOut);
    channel->liftOwnRestriction();
    channel->acknowledgeOwnRestrictionLifted();
    pump(100);

    // 9. setting off: the input stays as usual
    getSettings()->showOwnTimeoutInInput = false;
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(60));
    pump(100);
    check("with the setting off the input stays usable",
          !edit->isReadOnly() &&
              !edit->placeholderText().startsWith(timedOutPrefix));
    getSettings()->showOwnTimeoutInInput = true;
    channel->liftOwnRestriction();
    channel->acknowledgeOwnRestrictionLifted();
    pump(100);

    // 10. slow mode: a countdown at the end of the input, a queue for early
    // messages.
    auto badge = [&]() -> QString {
        for (auto *label : edit->findChildren<QLabel *>())
        {
            if (!label->isHidden())
            {
                return label->text();
            }
        }
        return {};
    };
    QStringList sent;
    channel->slowQueueSenderForTesting = [&sent](const QString &text,
                                                 const QString &) {
        sent.append(text);
    };
    auto sends = [&] {
        return int(sent.size());
    };
    const auto slowPrefix = before(uiText("Slow mode · %1"));
    TwitchChannelTestAccess::setSlowMode(*channel, 3);
    channel->setSendWait(0);
    pump(50);
    check("no wait: no countdown and nothing to queue",
          badge().isEmpty() && !channel->queueWhileSlow(QStringLiteral("hi")));
    channel->noteOwnMessageSent();
    pump(100);
    check("after a message: the countdown is in the input, which stays usable",
          badge().startsWith(slowPrefix) &&
              badge().contains(uiText("%1 s").arg(3)) && !edit->isReadOnly());
    shot(QStringLiteral("-slow"));
    // Long text wraps earlier next to the badge: the box grows, nothing is cut.
    // (A standalone box of the same kind: typing into the split's own input
    // needs app services this fixture lacks.)
    {
        ResizingTextEdit box;
        box.setFont(edit->font());
        box.setVerticalPadding(0.15);
        box.setSideBadge(uiText("Slow mode · %1").arg(uiText("%1 s").arg(1)));
        box.setPlainText(QString(QStringLiteral("ХЪЗАЫФХ ")).repeated(30));
        const int width = 560;
        box.resize(width, static_cast<QWidget &>(box).heightForWidth(width));
        box.show();
        pump(150);
        check("a long message next to the countdown is not cut off",
              box.document()->size().height() <= box.viewport()->height() + 1);
        box.grab().save(screenshotBase + "-slow-long.png", "PNG");
    }
    const auto before0 = sends();
    check("a message during the wait is queued",
          channel->queueWhileSlow(QStringLiteral("first")) &&
              channel->queueWhileSlow(QStringLiteral("second")) &&
              channel->slowQueueSize() == 2 && sends() == before0);
    pump(100);
    check("the countdown shows the queue",
          badge().contains(QStringLiteral("2")) &&
              badge() != uiText("Slow mode · %1").arg(uiText("%1 s").arg(3)));
    shot(QStringLiteral("-slow-queued"));
    QElapsedTimer clock;
    clock.start();
    pump(3600);
    const auto firstAt = clock.elapsed();
    check("the first queued message goes by itself when the wait ends",
          sends() == before0 + 1 && channel->slowQueueSize() == 1);
    check("…and starts the next wait", channel->sendWaitSeconds() >= 2);
    pump(3400);
    check("the second goes one wait later, in order",
          sends() == before0 + 2 && channel->slowQueueSize() == 0 &&
              clock.elapsed() - firstAt >= 2900 &&
              sent == QStringList{QStringLiteral("first"),
                                  QStringLiteral("second")});
    pump(3300);
    check("then the countdown disappears", badge().isEmpty());
    channel->setOwnTimedOut(QDateTime::currentDateTimeUtc().addSecs(60));
    channel->setSendWait(60);
    pump(50);
    check("a timeout is not a slow mode wait: nothing queued, no slow badge",
          !channel->queueWhileSlow(QStringLiteral("x")) &&
              !badge().startsWith(slowPrefix));
    channel->liftOwnRestriction();
    channel->acknowledgeOwnRestrictionLifted();
    channel->setSendWait(0);
    TwitchChannelTestAccess::setSlowMode(*channel, 0);
    channel->slowQueueSenderForTesting = nullptr;
    pump(100);

    QFile out(report);
    if (out.open(QIODevice::WriteOnly))
    {
        out.write(QJsonDocument(QJsonObject{{"mode", "own-timeout"},
                                            {"checks", checks}})
                      .toJson());
    }
    split.hide();
    split.setChannel(IndirectChannel(Channel::getEmpty()));
    return failed;
}

}  // namespace owntimeout
