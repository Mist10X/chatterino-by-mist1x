// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/layouts/MessageLayout.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/layouts/MessageLayoutContext.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/Selection.hpp"
#include "mocks/BaseApplication.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "providers/twitch/eventsub/MessageBuilder.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "Test.hpp"
#include "util/ChannelHelpers.hpp"

#include <QDebug>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QString>

#include <array>
#include <cmath>
#include <limits>
#include <memory>

using namespace chatterino;

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : windowManager(this->args, this->paths_, this->settings, this->theme,
                        this->fonts)
    {
    }

    WindowManager *getWindows() override
    {
        return &this->windowManager;
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    AccountController accounts;
    WindowManager windowManager;
};

constexpr int WIDTH = 300;

class MessageLayoutTest
{
public:
    // "aaaaaaaa bbbbbbbb cccccccc"
    MessageLayoutTest(const QString &text)
    {
        MessageBuilder builder;
        builder.append(
            std::make_unique<TextElement>(text, MessageElementFlag::Text));
        this->layout = std::make_unique<MessageLayout>(builder.release());
        MessageColors colors;
        this->layout->layout(
            {
                .messageColors = colors,
                .flags = MessageElementFlag::Text,
                .width = WIDTH,
                .scale = 1,
                .imageScale = 1,
                .selectedChannel = nullptr,
                .message = *this->layout->getMessage(),
            },
            false);
    }

    MockApplication mockApplication;
    std::unique_ptr<MessageLayout> layout;
};

}  // namespace

TEST(TextElement, BasicCase)
{
    auto test = MessageLayoutTest("abc");

    // Simulate we are clicking on the first word
    auto point = QPoint(WIDTH / 20, test.layout->getHeight() / 2);

    const auto *hoveredElement = test.layout->getElementAt(point);
    ASSERT_NE(hoveredElement, nullptr);

    const auto [wordStart, wordEnd] =
        test.layout->getWordBounds(hoveredElement, point);

    EXPECT_EQ(wordStart, 0);
    EXPECT_EQ(wordEnd, 3);
}

namespace {

MessagePtrMut indicatorMessage(MessageFlags flags)
{
    MessageBuilder builder;
    builder->flags = flags;
    builder.emplace<TimestampElement>(QTime(12, 34, 56));
    builder.emplace<TextElement>(
        "Saved context has been banned. This is ordinary message text, not a "
        "type discriminator.",
        MessageElementFlag::Text);
    return builder.release();
}

struct PaintedIndicator {
    QImage image;
    int height;
    size_t lines;
    QString copyText;
};

class PunishmentIndicatorTest : public ::testing::Test
{
protected:
    MockApplication app;

    void SetUp() override
    {
        this->app.settings.clientDetectionIcon = false;
        // This mock has no 7TV paint provider. Real IRC notices contain
        // username links, whose animated paint hook otherwise consults it.
        this->app.settings.displaySevenTVPaints = false;
        this->app.settings.hideModerated = false;
        this->app.settings.hideModerationActions = false;
    }

    PaintedIndicator paint(MessagePtr message, float scale = 1.F,
                           qreal dpr = 1., bool overlays = false)
    {
        MessageColors colors;
        colors.regularBg = QColor(24, 25, 32);
        colors.alternateBg = colors.regularBg;
        colors.regularText = Qt::white;
        colors.systemText = Qt::white;
        colors.linkText = Qt::white;
        colors.disabled = QColor(0, 0, 0, 180);
        colors.selection = QColor(30, 60, 150);
        colors.messageSeperator = QColor(80, 80, 90);
        MessagePreferences preferences;
        preferences.fadeMessageHistory = overlays;
        preferences.separateMessages = overlays;
        // Deliberately do not enable AutoMod or other highlight preferences.

        MessageLayout layout(message);
        layout.layout(
            {.messageColors = colors,
             .flags = {MessageElementFlag::Timestamp, MessageElementFlag::Text},
             .width = WIDTH,
             .scale = scale,
             .message = *message},
            false);
        constexpr int OFFSET = 7;
        QImage image(
            int(std::ceil(WIDTH * dpr)),
            int(std::ceil((layout.getHeight() + 2 * OFFSET + 1) * dpr)),
            QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(colors.regularBg);
        QPainter painter(&image);
        Selection selection;
        if (overlays)
        {
            selection = {{0, 0}, {0, layout.getLastCharacterIndex()}};
        }
        layout.paint({.painter = painter,
                      .selection = selection,
                      .colorProvider = ColorProvider::instance(),
                      .messageColors = colors,
                      .preferences = preferences,
                      .canvasWidth = WIDTH,
                      .y = OFFSET});
        painter.end();
        QString copied;
        layout.addSelectionText(copied, 0);
        return {std::move(image), layout.getHeight(), layout.getLineCount(),
                std::move(copied)};
    }

    static QRect redBounds(const QImage &image)
    {
        QRect bounds;
        for (int y = 0; y < image.height(); ++y)
        {
            // Only inspect the existing left margin, never text glyphs.
            for (int x = 0; x < std::min(8, image.width()); ++x)
            {
                const auto color = image.pixelColor(x, y);
                if (color.red() > 150 && color.green() < 120 &&
                    color.blue() < 150)
                {
                    const QRect pixel(x, y, 1, 1);
                    bounds = bounds.isNull() ? pixel : bounds.united(pixel);
                }
            }
        }
        return bounds;
    }
};

TEST_F(PunishmentIndicatorTest, UsesTypedFlagsRatherThanSystemOrMessageText)
{
    struct Case {
        const char *name;
        MessageFlags flags;
        bool stripe;
    };
    const std::array cases{
        Case{"ordinary chat mentioning a ban", {}, false},
        Case{"ordinary system", {MessageFlag::System}, false},
        Case{"generic moderation action",
             {MessageFlag::System, MessageFlag::ModerationAction},
             false},
        Case{"IRC ban or timeout",
             {MessageFlag::System, MessageFlag::Timeout},
             true},
        Case{"EventSub timeout",
             {MessageFlag::EventSub, MessageFlag::PubSub, MessageFlag::Timeout,
              MessageFlag::ModerationAction},
             true},
        Case{"deletion notice",
             {MessageFlag::System, MessageFlag::ModerationAction,
              MessageFlag::Deletion},
             true},
        Case{"disabled original",
             {MessageFlag::Disabled, MessageFlag::InvalidReplyTarget},
             false},
        Case{"AutoMod status",
             {MessageFlag::AutoMod, MessageFlag::ModerationAction},
             false},
        Case{"AutoMod header only",
             {MessageFlag::AutoModOffendingMessageHeader},
             false},
        Case{"held AutoMod text",
             {MessageFlag::AutoMod, MessageFlag::AutoModOffendingMessage},
             true},
        Case{"blocked-term hold",
             {MessageFlag::AutoMod, MessageFlag::AutoModBlockedTerm,
              MessageFlag::AutoModOffendingMessage},
             true},
        Case{"unban or untimeout",
             {MessageFlag::ModerationAction, MessageFlag::Untimeout},
             false},
        Case{"defensive untimeout wins",
             {MessageFlag::Timeout, MessageFlag::Untimeout},
             false},
        Case{"channel-wide clear",
             {MessageFlag::System, MessageFlag::ClearChat,
              MessageFlag::ModerationAction},
             false},
    };
    for (const auto &test : cases)
    {
        SCOPED_TRACE(test.name);
        EXPECT_EQ(!redBounds(this->paint(indicatorMessage(test.flags)).image)
                       .isNull(),
                  test.stripe);
    }
}

TEST_F(PunishmentIndicatorTest, StripeScalesInsideMarginWithoutChangingContent)
{
    for (const auto scale : {1.F, 1.5F})
    {
        for (const auto dpr : {1., 1.5})
        {
            SCOPED_TRACE(::testing::Message()
                         << "scale=" << scale << " dpr=" << dpr);
            const auto plain = this->paint(indicatorMessage({}), scale, dpr);
            const auto marked = this->paint(
                indicatorMessage({MessageFlag::Timeout}), scale, dpr);
            ASSERT_EQ(marked.height, plain.height);
            ASSERT_EQ(marked.lines, plain.lines);
            ASSERT_EQ(marked.copyText, plain.copyText);
            const auto stripe = redBounds(marked.image);
            ASSERT_FALSE(stripe.isNull());
            EXPECT_GE(stripe.left(), int(std::floor(scale * dpr)));
            EXPECT_LT(stripe.right(), int(std::ceil(3 * scale * dpr)));
            EXPECT_GE(stripe.top(), int(std::floor((7 + 3 * scale) * dpr)));
            EXPECT_LT(stripe.bottom(),
                      int(std::ceil((7 + marked.height - 3 * scale) * dpr)));
            EXPECT_LE(stripe.width(), int(std::ceil(2 * scale * dpr)));
            // Everything past the stripe's reserved margin must be identical:
            // timestamp, wrapping, text, background and copy geometry.
            for (int y = 0; y < marked.image.height(); ++y)
            {
                for (int x = int(std::ceil(4 * scale * dpr));
                     x < marked.image.width(); ++x)
                {
                    ASSERT_EQ(marked.image.pixel(x, y),
                              plain.image.pixel(x, y));
                }
            }
        }
    }
}

TEST_F(PunishmentIndicatorTest, SurvivesSelectionHistoryFadeAndDisabledOverlay)
{
    const auto marked = this->paint(
        indicatorMessage({MessageFlag::Timeout, MessageFlag::Disabled,
                          MessageFlag::RecentMessage}),
        1.F, 1., true);
    EXPECT_FALSE(redBounds(marked.image).isNull());
}

TEST_F(PunishmentIndicatorTest, HiddenActionsDoNotLeaveAnIndicator)
{
    this->app.settings.hideModerationActions = true;
    const auto hidden =
        this->paint(indicatorMessage({MessageFlag::System, MessageFlag::Timeout,
                                      MessageFlag::ModerationAction}));
    EXPECT_EQ(hidden.lines, 0);
    EXPECT_TRUE(redBounds(hidden.image).isNull());
}

TEST_F(PunishmentIndicatorTest,
       ActualIrcBuildersAndStackedReplacementRemainMarked)
{
    const auto time =
        QDateTime::fromString("2026-10-20T12:00:00Z", Qt::ISODate);
    this->app.settings.timeoutStackStyle =
        static_cast<int>(TimeoutStackStyle::StackHard);
    std::vector<MessagePtr> messages;
    for (const auto duration : {QStringLiteral("60"), QString{}})
    {
        SCOPED_TRACE(duration.isEmpty() ? "permanent ban" : "timeout");
        auto message = [&] {
            SCOPED_TRACE("build IRC punishment");
            MessageBuilder builder(timeoutMessage, "user", duration, false,
                                   time);
            return builder.release();
        }();
        ASSERT_TRUE(message->flags.has(MessageFlag::Timeout));
        [&] {
            SCOPED_TRACE("render IRC punishment");
            EXPECT_FALSE(redBounds(this->paint(message).image).isNull());
        }();
        addOrReplaceChannelTimeout(
            messages, message, time,
            [&](auto index, const auto &, const auto &replacement) {
                messages.at(index) = replacement;
            },
            [&](const auto &added) {
                messages.push_back(added);
            },
            false);
    }
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.front()->count, 2U);
    EXPECT_TRUE(messages.front()->flags.has(MessageFlag::Timeout));
    EXPECT_FALSE(redBounds(this->paint(messages.front()).image).isNull());

    auto original = indicatorMessage({});
    original->loginName = "user";
    original->displayName = "User";
    original->messageText = "Original saved message";
    original->id = "original-message-id";
    auto deletion = MessageBuilder::makeDeletionMessageFromIRC(original);
    EXPECT_TRUE(deletion->flags.has(MessageFlag::Deletion));
    EXPECT_FALSE(original->flags.has(MessageFlag::Deletion));
    EXPECT_FALSE(redBounds(this->paint(deletion).image).isNull());
}

TEST_F(PunishmentIndicatorTest,
       EventSubDeletionIsTypedAndSerializesWithoutTruncation)
{
    static_assert(sizeof(MessageFlags::Int) == sizeof(std::int64_t));
    static_assert(std::numeric_limits<MessageFlags::Int>::digits > 51);
    static_assert(static_cast<std::int64_t>(MessageFlag::GigantifiedEmote) ==
                  (1LL << 50));
    static_assert(static_cast<std::int64_t>(MessageFlag::Deletion) ==
                  (1LL << 51));

    eventsub::EventSubMessageBuilder builder(nullptr,
                                             QDateTime::currentDateTimeUtc());
    eventsub::lib::payload::channel_moderate::v2::Event event{};
    eventsub::lib::payload::channel_moderate::v2::Delete action{
        .userID = {"42"},
        .userLogin = {"user"},
        .userName = {"User"},
        .messageID = {"saved-id"},
        .messageBody = {"saved deleted text"}};
    eventsub::makeModerateMessage(builder, event, action);
    auto deletion = builder.release();
    ASSERT_TRUE(deletion->flags.has(MessageFlag::Deletion));
    EXPECT_TRUE(deletion->toJson()["flags"].toString().endsWith("|Deletion"));
    EXPECT_FALSE(redBounds(this->paint(deletion).image).isNull());
}

}  // namespace
