// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QFontMetricsF>
#include <QApplication>
#include <QPainter>
#include <QPushButton>
#include <QFrame>

// The own timeout in the input box (like Chatterino+): red with a countdown,
// green once it is over, normal after the first message. Synthetic channel,
// no IRC / PubSub connection; the state is driven through the same
// TwitchChannel entry points that IRC and PubSub use.

#include "Application.hpp"
#include "common/LinkParser.hpp"
#include "common/UiLanguage.hpp"
#include "messages/Image.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageParseArgs.hpp"
#include "providers/links/ImagePreview.hpp"
#include "singletons/ImageUploader.hpp"
#include "widgets/helper/UploadPreview.hpp"
#include "providers/twitch/IrcMessageHandler.hpp"
#include "providers/twitch/Mist1XMark.hpp"
#include "widgets/splits/InputHighlighter.hpp"
#include "controllers/spellcheck/SpellChecker.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/Scrollbar.hpp"
#include "widgets/helper/ResizingTextEdit.hpp"
#include "widgets/Label.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitInput.hpp"

#include <IrcMessage>
#include <QBuffer>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
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
    // A sent message "shows up in the chat" like Twitch's echo would, which
    // starts the next wait; echo=false: it never shows up (not delivered).
    bool echo = true;
    channel->slowQueueSenderForTesting = [&sent, &echo, &channel](
                                             const QString &text,
                                             const QString &) {
        sent.append(text);
        if (echo)
        {
            channel->noteOwnMessageSent();
        }
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
    {
        QLabel *badgeLabel = nullptr;
        for (auto *label : edit->findChildren<QLabel *>())
        {
            if (!label->isHidden())
            {
                badgeLabel = label;
            }
        }
        check("with a queue the countdown can be clicked to cancel",
              badgeLabel != nullptr && !badgeLabel->toolTip().isEmpty() &&
                  !badgeLabel->testAttribute(Qt::WA_TransparentForMouseEvents));
        check("cancelling gives back the newest queued message",
              channel->takeLastQueued() == QStringLiteral("second") &&
                  channel->slowQueueSize() == 1);
        channel->queueWhileSlow(QStringLiteral("second"));
    }
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

    // A message that does not go through starts no countdown, and a queued
    // one behind it is not stuck.
    echo = false;
    channel->noteOwnMessageSent();
    pump(50);
    channel->queueWhileSlow(QStringLiteral("third"));
    channel->queueWhileSlow(QStringLiteral("fourth"));
    pump(3400);
    check("a message that never shows up in the chat starts no countdown",
          sent.size() >= 3 && sent[2] == QStringLiteral("third") &&
              channel->sendWaitSeconds() == 0);
    pump(2700);
    check("…and the next queued one still goes",
          sent.size() == 4 && sent[3] == QStringLiteral("fourth") &&
              channel->slowQueueSize() == 0);
    pump(300);
    check("nothing left on the countdown", badge().isEmpty());
    echo = true;
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

    // 11. a live channel's name in a split header: dot, name, muted details
    // (no live stream in this fixture, so the label is shown on its own).
    {
        Label live(QStringLiteral("kopsteep"), FontStyle::UiMediumBold);
        live.setCentered(true);
        QPalette palette;
        palette.setColor(QPalette::Window, QColor("#20202a"));
        palette.setColor(QPalette::WindowText, QColor("#e8e8ee"));
        live.setPalette(palette);
        live.setAutoFillBackground(true);
        live.setLeadingDot(QColor(0xff, 0x4f, 0x5e));
        live.setDetails(QString::fromUtf8(
            "2 ч 14 мин · 22 326 зрителей · IRL"));
        live.resize(620, 30);
        live.show();
        pump(50);
        check("a live header shows the details next to the name",
              static_cast<QWidget &>(live).sizeHint().width() > 200);
        if (!screenshotBase.isEmpty())
        {
            live.grab().save(screenshotBase + "-live-header.png", "PNG");
            live.resize(260, 30);
            pump(30);
            live.grab().save(screenshotBase + "-live-header-narrow.png", "PNG");
        }
    }

    // 13. an outgoing raid above the input box, ended by Twitch's own word
    {
        TwitchChannel::RaidEvent raid;
        raid.id = QStringLiteral("raid-1");
        raid.targetId = QStringLiteral("123");
        raid.targetLogin = QStringLiteral("dangerlyoha");
        raid.targetDisplayName = QStringLiteral("dangerlyoha");
        raid.viewerCount = 13748;
        raid.forceRaidNowSeconds = 90;
        raid.receivedAt = QDateTime::currentDateTimeUtc();
        channel->setActiveRaid(raid);
        pump(300);
        QLabel *raidLabel = nullptr;
        QLabel *raidTime = nullptr;
        for (auto *label : split.findChildren<QLabel *>())
        {
            if (label->objectName() == u"raidStatusLabel")
            {
                raidLabel = label;
            }
            if (label->objectName() == u"raidStatusTime")
            {
                raidTime = label;
            }
        }
        check("the raid bar shows the target and viewers",
              raidLabel != nullptr && raidLabel->isVisible() &&
                  raidLabel->text().contains(u"dangerlyoha") &&
                  raidLabel->text().contains(u"748") &&
                  raidTime != nullptr && raidTime->text().contains(u':'));
        std::fprintf(stderr, "raid text: %s | %s\n",
                     raidLabel ? raidLabel->text().toUtf8().constData() : "",
                     raidTime ? raidTime->text().toUtf8().constData() : "");
        if (!screenshotBase.isEmpty())
        {
            split.getInput().grab().save(screenshotBase + "-raid.png", "PNG");
        }
        QString wentTo;
        pajlada::Signals::ScopedConnection went =
            channel->raidWent.connect([&](const QString &target) {
                wentTo = target;
            });
        channel->handleRaidUpdate(QJsonObject{
            {"type", "raid_go_v2"},
            {"topic", QStringLiteral("raid.%1").arg(channel->roomId())},
            {"raid", QJsonObject{{"id", "raid-1"}}},
        });
        pump(300);
        check("the raid bar goes when Twitch says the raid went",
              !channel->accessRaid()->has_value() &&
                  (raidLabel == nullptr || !raidLabel->isVisible()));
        check("the chat follows the raid to its target", wentTo == u"dangerlyoha");
    }

    // 14. a 7TV paint with a glow: no box where the glow used to be cut off
    {
        QGradientStops stops{{0.0, QColor("#ffcce0")}, {0.5, QColor("#bfe0ff")},
                             {1.0, QColor("#ffe6b0")}};
        LinearGradientPaint paint(QStringLiteral("Pastel"),
                                  QStringLiteral("test"), std::nullopt, stops,
                                  false, 90,
                                  {PaintDropShadow(0, 0, 5, QColor(191, 185,
                                                                   255, 191))});
        QFont font = QApplication::font();
        font.setPixelSize(15);
        font.setBold(true);
        const QFontMetricsF metrics(font);
        const QSizeF size(metrics.horizontalAdvance(QStringLiteral("@dapta4ke")),
                          metrics.height());
        qreal margin = 0;
        const auto pixmap = paint.getPixmap(QStringLiteral("@dapta4ke"), font,
                                            QColor("#c0c0ff"), size, 1, 1,
                                            false, &margin);
        const auto image = pixmap.toImage();
        // the outermost ring of the picture is clear: nothing is cut off
        int edge = 0;
        for (int x = 0; x < image.width(); ++x)
        {
            edge = std::max({edge, qAlpha(image.pixel(x, 0)),
                             qAlpha(image.pixel(x, image.height() - 1))});
        }
        for (int y = 0; y < image.height(); ++y)
        {
            edge = std::max({edge, qAlpha(image.pixel(0, y)),
                             qAlpha(image.pixel(image.width() - 1, y))});
        }
        check("a glowing 7TV name fades out inside its picture (no box)",
              margin > 0 && edge < 40);
        if (!screenshotBase.isEmpty())
        {
            QImage shot(image.size() + QSize(20, 20), QImage::Format_ARGB32);
            shot.fill(QColor("#18181b"));
            QPainter painter(&shot);
            painter.drawPixmap(10, 10, pixmap);
            painter.end();
            shot.save(screenshotBase + "-paint-glow.png", "PNG");
        }
    }

    // 15. punishments: a soft rounded box around the text — red ban, yellow
    //     timeout, grey deletion
    {
        auto punished = std::make_shared<TwitchChannel>(QStringLiteral("box_room"));
        split.setChannel(
            IndirectChannel(std::static_pointer_cast<Channel>(punished)));
        split.resize(560, 220);
        const auto now = QDateTime::currentDateTime();
        punished->addMessage(
            MessageBuilder(timeoutMessage, QStringLiteral("mainkrafter2000"),
                           QString(), false, now)
                .release(),
            MessageContext::Repost);
        punished->addMessage(
            MessageBuilder(timeoutMessage, QStringLiteral("furay_hd"),
                           QStringLiteral("600"), false, now)
                .release(),
            MessageContext::Repost);
        auto original = std::make_shared<Message>();
        original->loginName = QStringLiteral("vihkiins");
        original->displayName = QStringLiteral("vihkiins");
        original->messageText = QStringLiteral("DinoDance DinoDance PogChamp");
        original->id = QStringLiteral("deleted-id");
        punished->addMessage(MessageBuilder::makeDeletionMessageFromIRC(original),
                             MessageContext::Repost);
        pump(300);
        const auto image = split.getChannelView().grab().toImage();
        int red = 0;
        int yellow = 0;
        int grey = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor c = image.pixelColor(x, y);
                red += c.red() > c.green() + 30 && c.red() > c.blue() + 20 &&
                       c.green() < c.blue() + 25;
                yellow += c.red() > c.blue() + 30 && c.green() > c.blue() + 25;
                grey += std::abs(c.red() - c.green()) < 6 &&
                        c.blue() - c.red() >= 4 && c.blue() - c.red() < 14 &&
                        c.red() > 55 && c.red() < 90;
            }
        }
        std::fprintf(stderr, "punishment boxes: red=%d yellow=%d grey=%d\n", red,
                     yellow, grey);
        check("a ban gets a red box, a timeout a yellow one",
              red > 40 && yellow > 40);
        if (!screenshotBase.isEmpty())
        {
            image.save(screenshotBase + "-punishments.png", "PNG");
        }

        // a jump with room kept on top (the "while you were away" card)
        // leaves the message below that room, not at the very top
        std::vector<MessagePtr> filler;
        for (int i = 0; i < 120; ++i)
        {
            filler.push_back(makeSystemMessage(QStringLiteral("line %1").arg(i)));
            punished->addMessage(filler.back(), MessageContext::Repost);
        }
        pump(300);
        auto &view = split.getChannelView();
        const bool found = view.scrollToMessage(filler[60], 100);
        pump(400);
        const auto value =
            view.getScrollBar().getDesiredValue() - view.getScrollBar().getMinimum();
        const auto index = qreal(3 + 60);  // three punishments come first
        std::fprintf(stderr, "jump with room: value=%.2f target=%.0f\n", value,
                     index);
        check("a jump can keep room above the message",
              found && value < index - 1 && value > index - 12);
    }

    // 16. pictures: under image links in the chat, and the card above the
    //     input after a paste, with its delete button
    {
        namespace ip = imagepreview;
        const auto sites =
            ip::sites(QStringLiteral("https://www.kappa.lol/, segs.lol"));
        check("image sites are read from the setting",
              sites == QStringList{QStringLiteral("kappa.lol"),
                                   QStringLiteral("segs.lol")});
        check("a kappa.lol screenshot link is an image link",
              ip::isImageLink(QStringLiteral("https://kappa.lol/vCECFg"),
                              sites) &&
                  ip::isImageLink(QStringLiteral("kappa.lol/vCECFg"), sites) &&
                  ip::isImageLink(QStringLiteral("https://segs.lol/AbC12"),
                                  sites));
        check("kappa.lol's own pages are not",
              !ip::isImageLink(QStringLiteral("https://kappa.lol/"), sites) &&
                  !ip::isImageLink(QStringLiteral("https://kappa.lol/delete?ab"),
                                   sites) &&
                  !ip::isImageLink(QStringLiteral("https://kappa.lol/uploaders"),
                                   sites));
        const auto more =
            ip::sites(QStringLiteral("kappa.lol segs.lol eblo.id kappalol.fun"));
        check("eblo.id and kappalol.fun posts and albums are image links",
              ip::isImageLink(QStringLiteral("https://eblo.id/hH0WwpT"), more) &&
                  ip::isImageLink(QStringLiteral("https://eblo.id/AqUEzee"),
                                  more) &&
                  ip::isImageLink(QStringLiteral("https://kappalol.fun/Y8zpTA"),
                                  more) &&
                  ip::isImageLink(
                      QStringLiteral("https://kappalol.fun/group/Ab12Cd"), more));
        check("their own pages are not",
              !ip::isImageLink(QStringLiteral("https://eblo.id/videos"), more) &&
                  !ip::isImageLink(QStringLiteral("https://eblo.id/new"), more) &&
                  !ip::isImageLink(QStringLiteral("https://kappalol.fun/board"),
                                   more) &&
                  !ip::isImageLink(
                      QStringLiteral("https://kappalol.fun/profile/twenixz"),
                      more) &&
                  !ip::isImageLink(QStringLiteral("https://kappalol.fun/text/Ab12"),
                                   more));
        check("an image file on any site is one",
              ip::isImageLink(QStringLiteral("https://example.com/a/b.PNG"),
                              sites) &&
                  ip::isImageLink(
                      QStringLiteral("https://cdn.example.org/x.webp?v=2"),
                      sites));
        check("other pages and local addresses are not",
              !ip::isImageLink(QStringLiteral("https://example.com/page"),
                               sites) &&
                  !ip::isImageLink(QStringLiteral("http://192.168.1.5/a.png"),
                                   sites) &&
                  !ip::isImageLink(QStringLiteral("http://localhost/a.png"),
                                   sites));
        check("the same link written differently is one key",
              ip::key(QStringLiteral("http://www.kappa.lol/abc/")) ==
                  ip::key(QStringLiteral("https://kappa.lol/abc")));

        // a picture under the message
        QPixmap shot(300, 169);
        shot.fill(QColor(47, 168, 79));
        {
            QPainter painter(&shot);
            painter.fillRect(QRect(0, 0, 150, 169), QColor(40, 90, 200));
        }
        const auto link = QStringLiteral("https://kappa.lol/demo01");
        ip::insertForTesting(link, Image::fromResourcePixmap(shot));
        auto pictures =
            std::make_shared<TwitchChannel>(QStringLiteral("pictures_room"));
        split.setChannel(
            IndirectChannel(std::static_pointer_cast<Channel>(pictures)));
        split.resize(560, 420);
        MessageBuilder builder;
        builder.emplace<TextElement>(QStringLiteral("mist1x: смотри"),
                                     MessageElementFlag::Text);
        builder.addLink(*linkparser::parse(link), link);
        auto message = builder.release();
        const bool hasPreview =
            std::ranges::any_of(message->elements, [](const auto &element) {
                return dynamic_cast<ImagePreviewElement *>(element.get()) !=
                       nullptr;
            });
        pictures->addMessage(message, MessageContext::Repost);
        pump(400);
        const auto image = split.getChannelView().grab().toImage();
        int green = 0;
        int blue = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor c = image.pixelColor(x, y);
                green += std::abs(c.red() - 47) < 6 &&
                         std::abs(c.green() - 168) < 6 &&
                         std::abs(c.blue() - 79) < 6;
                blue += std::abs(c.red() - 40) < 6 &&
                        std::abs(c.green() - 90) < 6 &&
                        std::abs(c.blue() - 200) < 6;
            }
        }
        std::fprintf(stderr, "picture under the link: green=%d blue=%d\n",
                     green, blue);
        check("a screenshot link gets its picture under the message",
              hasPreview && green > 3000 && blue > 3000);
        if (!screenshotBase.isEmpty())
        {
            image.save(screenshotBase + "-picture.png", "PNG");
        }

        // the card above the input
        auto *uploader = getApp()->getImageUploader();
        auto *card = dynamic_cast<UploadPreview *>(
            split.getChannelView().findChild<QFrame *>(
                QStringLiteral("uploadPreview")));
        check("the chat has a card for pasted pictures",
              uploader != nullptr && card != nullptr && edit != nullptr);
        if (uploader != nullptr && card != nullptr && edit != nullptr)
        {
            QByteArray png;
            {
                QBuffer buffer(&png);
                buffer.open(QIODevice::WriteOnly);
                shot.save(&buffer, "PNG");
            }
            // (typing into this fixture's input is not possible: its text
            // handlers need services the fixture has not got)
            uploader->updated.invoke({
                .kind = ImageUploadUpdate::Kind::Started,
                .textEdit = edit,
                .imageData = png,
            });
            pump(100);
            const bool uploading =
                card->isVisible() && !card->deleteButton()->isVisible();
            const auto uploaded = QStringLiteral("https://kappa.lol/up42");
            uploader->updated.invoke({
                .kind = ImageUploadUpdate::Kind::Finished,
                .textEdit = edit,
                .link = uploaded,
                .deletionLink = QStringLiteral("https://kappa.lol/delete?K"),
            });
            pump(150);
            check("while uploading the card shows the picture, no delete yet",
                  uploading);
            check("after the upload it has the delete button",
                  card->isVisible() && card->deleteButton()->isVisible() &&
                      card->link() == uploaded);
            const auto geometry = card->geometry();
            const auto viewHeight = split.getChannelView().height();
            std::fprintf(stderr, "upload card: %d,%d %dx%d in a view %d high\n",
                         geometry.x(), geometry.y(), geometry.width(),
                         geometry.height(), viewHeight);
            check("the card sits at the bottom of the chat, over the input",
                  geometry.bottom() <= viewHeight &&
                      geometry.bottom() > viewHeight - 24 &&
                      geometry.height() >= 60);
            if (!screenshotBase.isEmpty())
            {
                split.grab().save(screenshotBase + "-upload-card.png", "PNG");
            }
            card->hide();

            // the same card on a bare input: it goes once the link is gone
            QWidget view;
            view.resize(500, 300);
            ResizingTextEdit box;
            UploadPreview bare(&view, &box, [](const QString &) {});
            view.show();
            box.setPlainText(QStringLiteral("look ") + uploaded +
                             QStringLiteral(" "));
            bare.apply({
                .kind = ImageUploadUpdate::Kind::Finished,
                .textEdit = &box,
                .link = uploaded,
            });
            pump(50);
            const bool stays = bare.isVisible();
            box.setPlainText(QStringLiteral("look again"));
            pump(50);
            check("the card goes away once the link leaves the input",
                  stays && !bare.isVisible());
            view.hide();
        }
    }

    // 17. whispers as cards: header, the sender's name as written, global
    //     badges only, a reply button; your own ones «to <Name>»
    {
        auto whispers = std::make_shared<Channel>(
            QStringLiteral("/whispers"), Channel::Type::TwitchWhispers);
        MessageBuilder theirs;
        theirs.emplace<TimestampElement>();
        theirs.emplace<TextElement>(QStringLiteral("OneShkof:"),
                                    MessageElementFlag::Username,
                                    QColor(QStringLiteral("#FF4D4D")),
                                    FontStyle::ChatMediumBold)
            ->setLink({Link::UserWhisper, QStringLiteral("OneShkof")});
        theirs.emplace<TextElement>(QStringLiteral("о, да, работает"),
                                    MessageElementFlag::Text);
        auto incoming = theirs.release();
        incoming->loginName = QStringLiteral("oneshkof");
        MessageBuilder::makeWhisperCard(*incoming, uiText("Whisper"),
                                        incoming->loginName);
        const auto &els = incoming->elements;
        const bool cardOk =
            !els.empty() &&
            dynamic_cast<NoticeIconElement *>(els.front().get()) &&
            dynamic_cast<WhisperReplyElement *>(els.back().get()) &&
            incoming->flags.has(MessageFlag::Whisper);
        whispers->addMessage(incoming, MessageContext::Repost);
        check("an incoming whisper is a card with a reply button", cardOk);

        MessageBuilder mine;
        mine.emplace<TimestampElement>();
        mine.emplace<TextElement>(QStringLiteral("Mist1X_x:"),
                                  MessageElementFlag::Username,
                                  MessageColor::Text, FontStyle::ChatMediumBold)
            ->setLink({Link::UserInfo, QStringLiteral("mist1x_x")});
        mine.emplace<TextElement>(QStringLiteral("ну шо работает?"),
                                  MessageElementFlag::Text);
        auto own = mine.release();
        MessageBuilder::makeWhisperCard(
            *own, uiText("Whisper to %1").arg(QStringLiteral("OneShkof")), {});
        whispers->addMessage(own, MessageContext::Repost);

        split.setChannel(IndirectChannel(whispers));
        split.resize(560, 260);
        pump(300);
        if (!screenshotBase.isEmpty())
        {
            split.getChannelView().grab().save(screenshotBase + "-whispers.png",
                                               "PNG");
        }
    }

    // 18. emotes in the input are set taller than the text: the input grows
    //     with them instead of cutting them off
    {
        struct Tall : QSyntaxHighlighter {
            using QSyntaxHighlighter::QSyntaxHighlighter;
            void highlightBlock(const QString &text) override
            {
                auto font = this->document()->defaultFont();
                if (font.pixelSize() > 0)
                {
                    font.setPixelSize(font.pixelSize() * 2);
                }
                else
                {
                    font.setPointSizeF(font.pointSizeF() * 2);
                }
                QTextCharFormat format;
                format.setFont(font);
                this->setFormat(0, int(text.size()), format);
            }
        };
        ResizingTextEdit box;
        box.resize(300, 40);
        box.setPlainText(QStringLiteral("peepoHappy peepoHappy"));
        const int plain = box.sizeHint().height();
        Tall tall(box.document());
        tall.rehighlight();
        const int grown = box.sizeHint().height();
        std::fprintf(stderr, "input height: plain=%d with emotes=%d\n", plain,
                     grown);
        check("taller emotes make the input taller", grown > plain + 4);
    }

    // 19. "/w <name> " at the start of the input is a «DM to <name>» chip
    {
        ResizingTextEdit box;
        box.setFont(edit != nullptr ? edit->font() : box.font());
        box.resize(520, 36);
        InputHighlighter highlighter(*getApp()->getSpellChecker(), nullptr);
        highlighter.setDocument(box.document());
        box.paintOverlay = [&](QPainter &painter) {
            highlighter.paintWhisperChip(painter, box);
        };
        box.setPlainText(QStringLiteral("/w oneshkof ну шо работает?"));
        box.show();
        pump(100);
        const int prefix = highlighter.whisperPrefix();
        if (!screenshotBase.isEmpty())
        {
            box.grab().save(screenshotBase + "-whisper-chip.png", "PNG");
        }
        box.setPlainText(QStringLiteral("/w oneshk"));
        pump(20);
        const int typing = highlighter.whisperPrefix();
        box.setPlainText(QStringLiteral("обычное сообщение"));
        pump(20);
        check("a typed-out /w name becomes a chip, not while typing the name",
              prefix == int(QStringLiteral("/w oneshkof ").size()) &&
                  typing == 0 && highlighter.whisperPrefix() == 0);
        box.hide();
    }

    // 20. the Chatterino by Mist1X badge: a marked client-nonce, nothing else
    {
        mist1xmark::resetForTesting();
        bool nonceOk = true;
        for (int i = 0; i < 50; ++i)
        {
            const auto nonce = mist1xmark::makeNonce();
            nonceOk = nonceOk && nonce.size() == 32 &&
                      mist1xmark::isMarked(nonce) &&
                      nonce == nonce.toLower();
        }
        check("own nonces look like the web chat's and carry the mark",
              nonceOk &&
                  !mist1xmark::isMarked(
                      QStringLiteral("5d2627b0cbe56fa05faf5420def4807d")));

        mist1xmark::remember(QStringLiteral("880001"));
        const bool marked = mist1xmark::badgeFor(QStringLiteral("880001")).has_value();
        const bool web = mist1xmark::badgeFor(QStringLiteral("880002")).has_value();
        const bool later = mist1xmark::isKnown(QStringLiteral("880001"));
        check("a marked message's sender gets the badge, others do not",
              marked && !web && later);
    }

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
