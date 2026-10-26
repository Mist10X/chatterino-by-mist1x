// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/settingspages/LeafyrinoPage.hpp"

#include "singletons/Settings.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace chatterino {

LeafyrinoPage::LeafyrinoPage()
{
    auto *outer = new QVBoxLayout;
    auto *inner = new QHBoxLayout;
    auto *view = GeneralPageView::withNavigation(this);
    this->view_ = view;

    inner->addWidget(view);
    auto *frame = new QFrame;
    frame->setLayout(inner);
    outer->addWidget(frame);
    this->setLayout(outer);

    this->initLayout(*view);
}

bool LeafyrinoPage::filterElements(const QString &query)
{
    if (this->view_)
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }

    return false;
}

void LeafyrinoPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle(uiText("Badges"));
    SettingWidget::checkbox(uiText("Folhinha"), s.showBadgesFolhinha)
        ->addKeywords({"folhinha", "folhinhabot"})
        ->setTooltip(uiText("Developer, admin, and Plus badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("FFZ:AP"), s.showBadgesFfzAp)
        ->addKeywords({"ffz", "ffzap", "frankerfacez"})
        ->setTooltip(uiText("Developer, helper, and supporter badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("DankChat"), s.showBadgesDankChat)
        ->addKeywords({"dankchat", "dank"})
        ->setTooltip(uiText("Dev/contributor, QA, and supporter badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Chatsen"), s.showBadgesChatsen)
        ->addKeywords({"chatsen"})
        ->setTooltip(uiText("Developer, supporter, artist, and Patreon badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Bluzyrino"), s.showBadgesBluzyrino)
        ->addKeywords({"bluzyrino", "bluzy"})
        ->setTooltip(uiText("Staff, contributor, founder, and supporter badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("JilChat"), s.showBadgesJilChat)
        ->addKeywords({"jilchat", "jil"})
        ->setTooltip(
            uiText("Owner, developer, founder, and membership milestones badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("BadgeHQ"), s.showBadgesBadgeHQ)
        ->addKeywords({"badgehq", "badge hq", "supporter", "founder"})
        ->setTooltip(uiText("Founder and supporter badges."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Limerino"), s.showBadgesLimerino)
        ->addKeywords({"limerino"})
        ->setTooltip(uiText("Founder and event badges."))
        ->addTo(layout);

    layout.addTitle(uiText("Client"));
    SettingWidget::checkbox(uiText("Show select badge button"), s.showSelectBadgeButton)
        ->addKeywords({"badge", "select", "button", "client", "picker"})
        ->setTooltip(uiText("Show the badge picker button in the chat input bar."))
        ->addTo(layout);

    layout.addTitle(uiText("YouTube"));
    SettingWidget::checkbox(uiText("Highlight Super Chats"),
                            s.highlightYouTubeSuperChats)
        ->addKeywords(
            {"youtube", "superchat", "super chat", "highlight", "donation"})
        ->setTooltip(
            uiText("Show Super Chats as a colored highlighted row (per tier)."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("White username in Super Chats"),
                            s.youtubeSuperChatWhiteName)
        ->addKeywords(
            {"youtube", "superchat", "super chat", "username", "white", "name"})
        ->setTooltip(
            uiText("Render the author name in white on the colored Super Chat "
            "row for readability."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Highlight memberships && gifts"),
                            s.highlightYouTubeMemberships)
        ->addKeywords(
            {"youtube", "membership", "member", "gift", "highlight", "green"})
        ->setTooltip(
            uiText("Give new memberships, milestones and gifted memberships a "
            "green highlight."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Color usernames by role"),
                            s.colorYouTubeUsernamesByRole)
        ->addKeywords({"youtube", "username", "color", "role", "moderator",
                       "member", "owner"})
        ->setTooltip(uiText("Color only moderators, members and the owner; regular "
                     "chatters stay a neutral grey. Off = all names grey.\n"
                     "Deleted messages follow the global \"hide moderated "
                     "messages\" setting."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Colorize usernames"), s.youtubeColorizeUsernames)
        ->addKeywords(
            {"youtube", "username", "colorize", "color", "nickname", "random"})
        ->setTooltip(
            uiText("Give each YouTube chatter a stable color based on their "
            "channel, like the Twitch colorize option. Roles still use "
            "their role color first."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Remove @ from usernames"), s.youtubeStripAtPrefix)
        ->addKeywords({"youtube", "username", "handle", "at", "prefix"})
        ->setTooltip(uiText("Hide the leading @ on YouTube handles in chat. Clicks, "
                     "mentions and the user popup still work with the full "
                     "name."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show @username in the split header"),
                            s.youtubeSplitHeaderUseHandle)
        ->addKeywords(
            {"youtube", "split", "header", "handle", "username", "channel"})
        ->setTooltip(uiText("Show the channel @handle in the split header instead of "
                     "the display name. Hovering always shows the handle."))
        ->addTo(layout);

    layout.addTitle(uiText("Usercard"));
    layout.addDescription(uiText("Choose which extra details appear on usercards."));

    SettingWidget::checkbox(uiText("Show follower count"), s.showUsercardFollowerCount)
        ->addKeywords({"usercard", "follower", "count"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show account creation date"),
                            s.showUsercardCreatedDate)
        ->addKeywords({"usercard", "created", "account", "date"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show last live"), s.showUsercardLastLive)
        ->setTooltip(uiText("Show when the user was last live. Hover the row to see "
                     "the stream title."))
        ->addKeywords({"usercard", "last", "live", "stream"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show live viewer count"),
                            s.showUsercardLiveViewerCount)
        ->setTooltip(uiText("When enabled, replaces the red live dot next to the "
                     "username with the live viewer count."))
        ->addKeywords({"usercard", "live", "viewer", "count", "indicator"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show user color"), s.showUsercardColor)
        ->setTooltip(uiText("Show the user's Twitch chat color."))
        ->addKeywords({"usercard", "color", "chat"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show 7TV paint"), s.showUsercardSevenTVPaint)
        ->setTooltip(uiText("Show the user's equipped 7TV paint on the usercard."))
        ->addKeywords({"usercard", "7tv", "seventv", "paint", "cosmetic"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show Twitch status"), s.showUsercardStatus)
        ->setTooltip(
            uiText("Show whether the user is Staff, Partner, Affiliate, or Regular."))
        ->addKeywords({"usercard", "status", "staff", "partner", "affiliate"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show chatter count"), s.showUsercardChatterCount)
        ->setTooltip(uiText("Show the current chatter count when available."))
        ->addKeywords({"usercard", "chatter", "count"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show followage"), s.showUsercardFollowage)
        ->setTooltip(uiText("Show how long the user has followed this channel. Only "
                     "available when you are the broadcaster or a moderator."))
        ->addKeywords({"usercard", "followage", "follow"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show follow button"), s.showFollowButtonInUsercard)
        ->setTooltip(uiText("Show a follow/unfollow button on usercards. Requires "
                     "Moltorino auth (Settings → Moltorino → Authentication)."))
        ->addKeywords({"usercard", "follow", "button"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Confirm before unfollowing from usercard"),
                            s.confirmUnfollowFromUsercard)
        ->conditionallyEnabledBy(s.showFollowButtonInUsercard)
        ->setTooltip(uiText("Ask before the usercard follow button unfollows a user. "
                     "The /unfollow command still runs without a prompt."))
        ->addKeywords({"usercard", "follow", "unfollow", "confirm"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show gift sub gifter"), s.showUsercardSubGiftGifter)
        ->setTooltip(uiText("When the user has an active gifted subscription in a "
                     "channel, show who gifted it on the usercard."))
        ->addKeywords({"usercard", "gift", "gifter", "subscription", "sub"})
        ->addTo(layout);

    layout.addTitle(uiText("Messages per second"));
    SettingWidget::checkbox(uiText("Show messages-per-second (mps) overlay in splits"),
                            s.showSplitMps)
        ->setTooltip(uiText("Shows a faint overlay label (e.g. \"12 mps\") with the "
                     "average number of messages per second over the "
                     "selected window."))
        ->addTo(layout);

    SettingWidget::dropdown(uiText("MPS averaging window"), s.splitMpsWindow)
        ->conditionallyEnabledBy(s.showSplitMps)
        ->setTooltip(uiText("Longer windows smooth out Twitch burst delivery so the "
                     "counter does not spike and drop to zero between bursts."))
        ->addTo(layout);

    SettingWidget::dropdown(uiText("MPS overlay position"), s.splitMpsCorner)
        ->conditionallyEnabledBy(s.showSplitMps)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show 0 mps"), s.showSplitMpsWhenZero)
        ->conditionallyEnabledBy(s.showSplitMps)
        ->addTo(layout);

    layout.addTitle(uiText("Events"));
    layout.addDescription(
        uiText("Channel event messages in chat. Requires Moltorino auth "
        "(Settings → Moltorino → Authentication), experimental EventSub "
        "enabled under Settings → General, and an app restart after turning "
        "EventSub on."));

    SettingWidget::checkbox(uiText("Show follow notifications in chat"),
                            s.showFollowEventsInChat)
        ->setTooltip(
            uiText("Show a chat message when someone follows the channel. Only "
            "available when you are the broadcaster or a moderator."))
        ->addKeywords({"follow", "events", "eventsub", "chat", "moderator"})
        ->addTo(layout);

    layout.addTitle(uiText("Commands"));
    SettingWidget::checkbox(uiText("Show command suggestions while typing"),
                            s.showCommandSuggestions)
        ->setTooltip(uiText("Show a compact command suggestion strip above the "
                     "message input."))
        ->addTo(layout);

    layout.addSubtitle(uiText("Supibot commands"));
    SettingWidget::checkbox(uiText("Include Supibot commands in suggestions"),
                            s.includeSupibotCommands)
        ->addKeywords({"supibot", "supinic", "command", "suggestion"})
        ->setTooltip(uiText("Suggest Supibot $ commands after you type $, in "
                     "channels where Supibot is active."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show Supibot command aliases"),
                            s.showSupibotCommandAliases)
        ->addKeywords({"supibot", "alias", "command", "suggestion"})
        ->setTooltip(uiText("Include built-in shortcuts such as $cf. Your own "
                     "aliases are suggested separately after you type $$."))
        ->conditionallyEnabledBy(s.includeSupibotCommands)
        ->addTo(layout);

    layout.addSubtitle(uiText("PotatBotat"));
    SettingWidget::checkbox(uiText("Include Potat commands in suggestions"),
                            s.includePotatCommands)
        ->setTooltip(
            uiText("Suggest Potat commands after you type that channel's prefix, "
            "including custom commands that use it, when PotatBotat is "
            "joined."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show Potat command aliases"),
                            s.showPotatCommandAliases)
        ->setTooltip(uiText("Include command shortcuts in Potat suggestions."))
        ->conditionallyEnabledBy(s.includePotatCommands)
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show Potat commands without a prefix"),
                            s.showPotatUnprefixedCommands)
        ->setTooltip(uiText("Also suggest this channel's custom commands that do "
                     "not start with its prefix."))
        ->addKeywords({"potat", "custom", "prefix", "command"})
        ->conditionallyEnabledBy(s.includePotatCommands)
        ->addTo(layout);

    layout.addTitle(uiText("Miscellaneous"));
    SettingWidget::checkbox(uiText("Use message colors for tab alerts"),
                            s.colorTabHighlightsByMessage)
        ->setTooltip(uiText("When a message highlights a tab, use that highlight "
                     "color for the tab alert line."))
        ->addKeywords({"tab", "alert", "highlight", "color"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Wrap links at breaks"), s.wrapLinksAtBreaks)
        ->setTooltip(uiText("Let URLs wrap at /, ?, &, #, and = instead of staying "
                     "on one line."))
        ->addKeywords({"url", "link", "wrap", "break"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show full date when hovering timestamps"),
                            s.showTimestampDateTooltip)
        ->setTooltip(uiText("Show the full message date and time when hovering a "
                     "timestamp. Uses your message timestamp format for the "
                     "time portion."))
        ->addKeywords({"timestamp", "date", "tooltip", "hover"})
        ->addTo(layout);

    layout.addStretch();

    // Invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
