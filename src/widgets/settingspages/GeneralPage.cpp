// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/settingspages/GeneralPage.hpp"
#include "RunGui.hpp"
#include "widgets/dialogs/WelcomeDialog.hpp"
#include "widgets/helper/ArchiveActions.hpp"
#include "widgets/helper/UpdateBanner.hpp"
#include "singletons/Updates.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"  // IWYU pragma: keep
#include "common/Modes.hpp"
#include "common/Version.hpp"
#include "controllers/hotkeys/HotkeyCategory.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/CrashHandler.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/NativeMessaging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "util/FuzzyConvert.hpp"
#include "util/Helpers.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/helper/FontSettingWidget.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QSignalBlocker>

namespace {

using namespace chatterino;
using namespace chatterino::literals;


#ifdef Q_OS_WIN
const QString META_KEY = u"Windows"_s;
#else
const QString META_KEY = u"Meta"_s;
#endif

const QStringList ZOOM_LEVELS = {
    "0.5x", "0.6x", "0.7x", "0.8x",  "0.9x",  "Default", "1.2x", "1.4x",
    "1.6x", "1.8x", "2x",   "2.33x", "2.66x", "3x",      "3.5x", "4x",
};

void addKeyboardModifierSetting(GeneralPageView &layout, const QString &title,
                                EnumSetting<Qt::KeyboardModifier> &setting)
{
    layout.addDropdown<std::underlying_type_t<Qt::KeyboardModifier>>(
        title, {"None", "Shift", "Control", "Alt", META_KEY}, setting,
        [](int index) {
            switch (index)
            {
                case Qt::ShiftModifier:
                    return 1;
                case Qt::ControlModifier:
                    return 2;
                case Qt::AltModifier:
                    return 3;
                case Qt::MetaModifier:
                    return 4;
                default:
                    return 0;
            }
        },
        [](DropdownArgs args) {
            switch (args.index)
            {
                case 1:
                    return Qt::ShiftModifier;
                case 2:
                    return Qt::ControlModifier;
                case 3:
                    return Qt::AltModifier;
                case 4:
                    return Qt::MetaModifier;
                default:
                    return Qt::NoModifier;
            }
        },
        false);
}
}  // namespace

namespace chatterino {

namespace {

/// The program's own Cache folder, or a folder this program made for its
/// cache: never the program folder, its settings or logs, or a parent of them.
bool isSafeCacheFolder(const QString &path)
{
    const auto &paths = getApp()->getPaths();
    const auto clean = QDir::cleanPath(QDir(path).absolutePath());
    const auto root = QDir::cleanPath(paths.rootAppDataDirectory);
    const auto inside = [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) == 0 ||
               a.startsWith(b + u'/', Qt::CaseInsensitive);
    };
    if (inside(root, clean))
    {
        return false;  // the program folder itself or above it
    }
    if (inside(clean, root))
    {
        return inside(clean, QDir::cleanPath(QDir(root).filePath("Cache")));
    }
    return QFileInfo(clean).fileName().compare(
               QStringLiteral("Chatterino by Mist1X cache"),
               Qt::CaseInsensitive) == 0;
}

}  // namespace

GeneralPage::GeneralPage()
{
    auto *y = new QVBoxLayout;
    auto *x = new QHBoxLayout;
    auto *view = GeneralPageView::withNavigation(this);
    this->view_ = view;
    x->addWidget(view);
    auto *z = new QFrame;
    z->setLayout(x);
    y->addWidget(z);
    this->setLayout(y);

    this->initLayout(*view);

    this->initExtra();
}

bool GeneralPage::filterElements(const QString &query)
{
    if (this->view_)
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }
    else
    {
        return false;
    }
}

void GeneralPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Language and theme"));

    SettingWidget::dropdown(uiText("Language (requires restart)"), s.uiLanguage,
                            {{uiText("Русский"), QString("ru")},
                             {"English", QString("en")}})
        ->setTooltip(uiText("Changes the interface language after restarting Leafyrino. "
                     "Chat messages, usernames, channels, emotes and fonts are unchanged."))
        ->addKeywords({"language", uiText("язык"), uiText("русский"), "English"})
        ->addTo(layout);
    // the new language needs a restart: offer it right away (in both
    // languages, the new one is not active yet)
    s.uiLanguage.connect(
        [this](const auto &value, auto) {
            // not in tests and previews (windows never put on the screen),
            // not when going back to the language in use (Cancel)
            if (value == activeUiLanguage() ||
                this->window()->testAttribute(Qt::WA_DontShowOnScreen) ||
                this->testAttribute(Qt::WA_DontShowOnScreen))
            {
                return;
            }
            const auto answer = QMessageBox::question(
                this->window(),
                QStringLiteral("Язык / Language"),
                QStringLiteral("Язык сменится после перезапуска. Перезапустить "
                               "сейчас?\n\nThe language changes after a "
                               "restart. Restart now?"));
            if (answer == QMessageBox::Yes)
            {
                getSettings()->requestSave();
                restartApp();
            }
        },
        this->managedConnections_, false);

    {
        auto *themes = getApp()->getThemes();
        auto available = themes->availableThemes();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        available.emplace(available.begin(), "System", "System");
#endif

        SettingWidget::dropdown(uiText("Theme"), themes->themeName, available)
            ->addTo(layout);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        SettingWidget::dropdown(uiText("Dark system theme"),
                                themes->darkSystemThemeName,
                                themes->availableThemes())
            ->setTooltip(uiText("This theme is selected if your system is in a dark "
                         "theme and you enabled the adaptive 'System' theme."))
            ->conditionallyEnabledBy(themes->themeName, "System")
            ->addTo(layout);

        SettingWidget::dropdown(uiText("Light system theme"),
                                themes->lightSystemThemeName,
                                themes->availableThemes())
            ->setTooltip(uiText("This theme is selected if your system is in a light "
                         "theme and you enabled the adaptive 'System' theme."))
            ->conditionallyEnabledBy(themes->themeName, "System")
            ->addTo(layout);
#endif
    }

    layout.addDropdown<float>(
        uiText("Zoom"), ZOOM_LEVELS, s.uiScale,
        [](auto val) {
            if (val == 1)
            {
                return QString("Default");
            }
            else
            {
                return QString::number(val) + "x";
            }
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 1.f);
        });
    SettingWidget::checkbox(uiText("Use legacy scaling (requires restart)"),
                            s.useLegacyScaling)
        ->setTooltip(
            uiText("Approximate older Chatterino scaling by treating the display as "
            "96 DPI (Qt::AA_Use96Dpi). Useful if the UI or text feels wrong "
            "after an update on a high-DPI or fractional-scale display. "
            "Restart required. You can also pass "
            "--use-old-scaling on the command line for a single run."))
        ->addKeywords({"dpi", "hidpi", "high dpi", "fractional"})
        ->addTo(layout);
    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Tabs"));
    ComboBox *tabDirectionDropdown =
        layout.addDropdown<std::underlying_type_t<NotebookTabLocation>>(
            uiText("Tab layout"), {"Top", "Left", "Right", "Bottom"}, s.tabDirection,
            [](auto val) {
                switch (val)
                {
                    case NotebookTabLocation::Top:
                        return "Top";
                    case NotebookTabLocation::Left:
                        return "Left";
                    case NotebookTabLocation::Right:
                        return "Right";
                    case NotebookTabLocation::Bottom:
                        return "Bottom";
                }

                return "";
            },
            [](auto args) {
                if (args.value == "Bottom")
                {
                    return NotebookTabLocation::Bottom;
                }
                else if (args.value == "Left")
                {
                    return NotebookTabLocation::Left;
                }
                else if (args.value == "Right")
                {
                    return NotebookTabLocation::Right;
                }
                else
                {
                    return NotebookTabLocation::Top;
                }
            },
            false);
    tabDirectionDropdown->setMinimumWidth(
        tabDirectionDropdown->minimumSizeHint().width());

    layout.addDropdown<std::underlying_type_t<NotebookTabVisibility>>(
        uiText("Tab visibility"), {"All tabs", "Only live tabs"}, s.tabVisibility,
        [](auto val) {
            switch (val)
            {
                case NotebookTabVisibility::LiveOnly:
                    return "Only live tabs";
                case NotebookTabVisibility::AllTabs:
                default:
                    return "All tabs";
            }
        },
        [](auto args) {
            if (args.value == "Only live tabs")
            {
                return NotebookTabVisibility::LiveOnly;
            }
            else
            {
                return NotebookTabVisibility::AllTabs;
            }
        },
        false, "Choose which tabs are visible in the notebook");

    SettingWidget::dropdown(uiText("Tab style"), s.tabStyle)->addTo(layout);
    SettingWidget::dropdown(uiText("Channel avatars on tabs"), s.tabAvatarStyle)
        ->setTooltip(uiText("Show the Twitch channel's avatar on its tab. With "
                     "\"Avatar only\", the other tabs show just the avatar "
                     "and the name appears on hover."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Extend wrapped tabs"), s.growWrappedNotebookLines)
        ->setTooltip(uiText("When horizontal tabs are wrapped, extend the line for "
                     "the whole width of the window."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Language and theme"));
    layout.addWidget(new FontSettingWidget(s.chatFontFamily, s.chatFontSize,
                                           s.chatFontWeight),
                     {"font", "weight", "size"});

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Messages"));
    SettingWidget::inverseCheckbox(uiText("Show message reply context"),
                                   s.hideReplyContext)
        ->setTooltip(
            uiText("This setting will only affect how messages are shown. You can "
            "reply to a message regardless of this setting."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show message reply button"), s.showReplyButton)
        ->setTooltip(uiText("Show a reply button next to every chat message"))
        ->addTo(layout);

    auto removeTabSeq = getApp()->getHotkeys()->getDisplaySequence(
        HotkeyCategory::Window, "removeTab");
    QString removeTabShortcut = "an assigned hotkey (Window -> remove tab)";
    if (!removeTabSeq.isEmpty())
    {
        removeTabShortcut =
            removeTabSeq.toString(QKeySequence::SequenceFormat::NativeText);
    }

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Tabs"));
    SettingWidget::checkbox(uiText("Show tab close button"), s.showTabCloseButton)
        ->setTooltip(
            uiText("When disabled, the x to close a tab will be hidden.\nTabs can "
            "still be closed by right-clicking or pressing ") +
            removeTabShortcut + ".")
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Window"));
    SettingWidget::checkbox(uiText("Always on top"), s.windowTopMost)
        ->setTooltip(uiText("Always keep Leafyrino as the top window."))
        ->addTo(layout);

#ifdef USEWINSDK
    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Startup and tray"));
    SettingWidget::checkbox(uiText("Start with Windows"), s.autorun)
        ->setTooltip(uiText("Start Leafyrino when your computer starts."))
        ->addTo(layout);
#endif
    if (!BaseWindow::supportsCustomWindowFrame())
    {
        auto settingsSeq = getApp()->getHotkeys()->getDisplaySequence(
            HotkeyCategory::Window, "openSettings");
        QString shortcut = " (no key bound to open them otherwise)";

        if (!settingsSeq.isEmpty())
        {
            shortcut = QStringLiteral(" (%1 to show)")
                           .arg(settingsSeq.toString(
                               QKeySequence::SequenceFormat::NativeText));
        }

        routeSetting(layout, QStringLiteral("Appearance"),
                     QStringLiteral("Window"));
        SettingWidget::inverseCheckbox(uiText("Show preferences button") + shortcut,
                                       s.hidePreferencesButton)
            ->addTo(layout);

        SettingWidget::inverseCheckbox(uiText("Show user button"), s.hideUserButton)
            ->addTo(layout);
    }

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Tabs"));
    SettingWidget::checkbox(uiText("Mark tabs with live channels"), s.showTabLive)
        ->setTooltip(uiText("Shows a red dot in the top right corner of a tab to "
                     "indicate one of the channels in the tab is live."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Scrolling"));

    layout.addDropdown<float>(
        uiText("Pause on mouse hover"),
        {"Disabled", "0.5s", "1s", "2s", "5s", "Indefinite"},
        s.pauseOnHoverDuration,
        [](auto val) {
            if (val < -0.5f)
            {
                return QString("Indefinite");
            }
            else if (val < 0.001f)
            {
                return QString("Disabled");
            }
            else
            {
                return QString::number(val) + "s";
            }
        },
        [](auto args) {
            if (args.index == 0)
            {
                return 0.0f;
            }
            else if (args.value == "Indefinite")
            {
                return -1.0f;
            }
            else
            {
                return fuzzyToFloat(args.value,
                                    std::numeric_limits<float>::infinity());
            }
        });
    addKeyboardModifierSetting(layout, "Pause while holding a key",
                               s.pauseChatModifier);
    layout.addDropdown<float>(
        uiText("Mousewheel scroll speed"), {"0.5x", "0.75x", "Default", "1.5x", "2x"},
        s.mouseScrollMultiplier,
        [](auto val) {
            if (val == 1)
            {
                return QString("Default");
            }
            else
            {
                return QString::number(val) + "x";
            }
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 1.f);
        });

    SettingWidget::checkbox(uiText("Smooth scrolling"), s.enableSmoothScrolling)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Smooth scrolling on new messages"),
                            s.enableSmoothScrollingNewMessages)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Input Box"));
    SettingWidget::checkbox(uiText("Show input when it's empty"), s.showEmptyInput)
        ->setTooltip(uiText("Show the chat box even when there is nothing typed."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show message length while typing"),
                            s.showMessageLength)
        ->setTooltip(
            uiText("Show how many characters are currently in your input box.\n"
            "Useful for making sure you don't go past the 500 character Twitch "
            "limit, or a lower limit enforced by a moderation bot"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Slow mode"));
    SettingWidget::checkbox(uiText("Show countdown on slow mode or when timed out"),
                            s.showSendWaitTimer)
        ->setTooltip(uiText("Show how long you may need to wait before being able to "
                     "send in a Twitch channel again if the channel is in slow "
                     "mode or if you have been timed out"))
        ->addKeywords({"slowmode", "timeout"})
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Sending"));
    SettingWidget::checkbox(uiText("Allow sending duplicate messages"),
                            s.allowDuplicateMessages)
        ->setTooltip(
            uiText("Allow a single message to be repeatedly sent without any changes."))
        ->addTo(layout);

    layout.addDropdown<std::underlying_type_t<MessageOverflow>>(
        uiText("Message overflow"), {"Highlight", "Prevent", "Allow"},
        s.messageOverflow,
        [](auto index) {
            return index;
        },
        [](auto args) {
            return static_cast<MessageOverflow>(args.index);
        },
        false,
        "Specify how Leafyrino will handle messages that exceed Twitch "
        "message limits");
    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Mouse"));
    layout.addDropdown<std::underlying_type_t<UsernameRightClickBehavior>>(
        uiText("Username right-click behavior"),
        {
            "Reply",
            "Mention",
            "Ignore",
        },
        s.usernameRightClickBehavior,
        [](auto index) {
            return index;
        },
        [](auto args) {
            return static_cast<UsernameRightClickBehavior>(args.index);
        },
        false,
        "Specify how Leafyrino will handle right-clicking a username in "
        "chat when not holding the modifier.");
    layout.addDropdown<std::underlying_type_t<UsernameRightClickBehavior>>(
        uiText("Username right-click with modifier behavior"),
        {
            "Reply",
            "Mention",
            "Ignore",
        },
        s.usernameRightClickModifierBehavior,
        [](auto index) {
            return index;
        },
        [](auto args) {
            return static_cast<UsernameRightClickBehavior>(args.index);
        },
        false,
        "Specify how Leafyrino will handle right-clicking a username in "
        "chat when holding down the modifier.");
    layout.addDropdown<std::underlying_type_t<Qt::KeyboardModifier>>(
        uiText("Modifier for alternate right-click action"),
        {"Shift", "Control", "Alt", META_KEY}, s.usernameRightClickModifier,
        [](int index) {
            switch (index)
            {
                case Qt::ShiftModifier:
                    return 0;
                case Qt::ControlModifier:
                    return 1;
                case Qt::AltModifier:
                    return 2;
                case Qt::MetaModifier:
                    return 3;
                default:
                    return 0;
            }
        },
        [](DropdownArgs args) {
            switch (args.index)
            {
                case 0:
                    return Qt::ShiftModifier;
                case 1:
                    return Qt::ControlModifier;
                case 2:
                    return Qt::AltModifier;
                case 3:
                    return Qt::MetaModifier;
                default:
                    return Qt::NoModifier;
            }
        },
        false);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Scrolling"));
    SettingWidget::checkbox(uiText("Hide scrollbar thumb"), s.hideScrollbarThumb)
        ->setTooltip(uiText("Hiding the scrollbar thumb (the handle you can drag) "
                     "will disable all mouse interaction in the scrollbar."))
        ->addKeywords({"scroll bar"})
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide scrollbar highlights"),
                            s.hideScrollbarHighlights)
        ->addKeywords({"scroll bar"})
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Input Box"));
    SettingWidget::checkbox(
        uiText("Pulse text input when one of your messages is successfully sent"),
        s.pulseTextInputOnSelfMessage)
        ->setTooltip(
            uiText("Pulses the text input in a green color whenever a message of "
            "yours is successfully sent in the matching channel."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Messages"));
    SettingWidget::checkbox(uiText("Separate with lines"), s.separateMessages)
        ->setTooltip(
            uiText("Adds a line between each message to help better tell them apart."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Alternate background color"), s.alternateMessages)
        ->setTooltip(uiText("Slightly change the background behind every other "
                     "message to help better tell them apart."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Reduce opacity of message history"),
                            s.fadeMessageHistory)
        ->setTooltip(
            uiText("Reduce opacity of messages that were posted before Leafyrino "
            "was started or while re-connecting."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Hidden messages"));
    SettingWidget::checkbox(uiText("Hide deleted messages"), s.hideModerated)
        ->setTooltip(
            uiText("When enabled, messages deleted by moderators will be hidden."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Messages"));
    SettingWidget::checkbox(uiText("Hide message timestamps when channel is live"),
                            s.hideMessageTimestampsWhenLive)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Correct ASCII art wrapping"), s.wrapAsciiArt)
        ->setTooltip(uiText("Limit the width of messages containing ASCII art to "
                     "match the width of Twitch web chat."))
        ->addTo(layout);

    layout.addDropdown<QString>(
        uiText("Message timestamp format"),
        {"Disable", "h:mm", "hh:mm", "h:mm a", "hh:mm a", "h:mm:ss", "hh:mm:ss",
         "h:mm:ss a", "hh:mm:ss a", "h:mm:ss.zzz", "h:mm:ss.zzz a",
         "hh:mm:ss.zzz", "hh:mm:ss.zzz a"},
        s.timestampFormat,
        [](auto val) {
            return getSettings()->showTimestamps.getValue()
                       ? val
                       : QString("Disable");
        },
        [](auto args) {
            getSettings()->showTimestamps.setValue(args.index != 0);

            return args.index == 0 ? getSettings()->timestampFormat.getValue()
                                   : args.value;
        },
        true, "a = am/pm, zzz = milliseconds");

    SettingWidget::dropdown(uiText("Message timestamp size"), s.timestampSize)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show header timestamps"), s.showHeaderTimestamps)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("What to show in chat"));
    SettingWidget::checkbox(uiText("Show announcement header"),
                            s.showAnnouncementHeader)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show subscription header"),
                            s.showSubscriptionHeader)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show watch streak header"), s.showWatchStreakHeader)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show Twitch GIFs"), s.showTwitchGifs)
        ->setTooltip(uiText("Twitch GIFs will be shown inline. When disabled, they're "
                     "shown as links."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Messages"));
    layout.addDropdown<int>(
        uiText("Limit message height"),
        {"Never", "2 lines", "3 lines", "4 lines", "5 lines"},
        s.collpseMessagesMinLines,
        [](auto val) {
            return val ? QString::number(val) + " lines" : QString("Never");
        },
        [](auto args) {
            return fuzzyToInt(args.value, 0);
        });
    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Hidden messages"));
    layout.addDropdown<int>(
        uiText("Limit length of deleted messages"),
        {"No limit", "50 characters", "100 characters", "200 characters",
         "300 characters", "400 characters"},
        s.deletedMessageLengthLimit,
        [](auto val) {
            return val ? QString::number(val) + " characters"
                       : QString("No limit");
        },
        [](const auto &args) {
            return fuzzyToInt(args.value, 0);
        },
        true,
        {"Limits the amount of characters displayed in deleted messages "
         "when announced via system message."});

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Messages"));
    layout.addSeparator();

    SettingWidget::checkbox(uiText("Draw a line below the most recent message before "
                            "switching applications."),
                            s.showLastMessageIndicator)
        ->setTooltip(uiText("Adds an underline below the most recent message "
                     "sent before you tabbed out of Leafyrino."))
        ->addTo(layout);

    SettingWidget::dropdown(uiText("Line style"), s.lastMessagePattern)->addTo(layout);

    SettingWidget::colorButton(uiText("Line color"), s.lastMessageColor)->addTo(layout);

    routeSetting(layout, QStringLiteral("Emotes and badges"),
                 QStringLiteral("Emotes"));

    SettingWidget::checkbox(uiText("Enable"), s.enableEmoteImages)->addTo(layout);

    SettingWidget::checkbox(uiText("Animate"), s.animateEmotes)->addTo(layout);

    SettingWidget::checkbox(uiText("Animate only when Leafyrino is focused"),
                            s.animationsWhenFocused)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Enable zero-width emotes"), s.enableZeroWidthEmotes)
        ->setTooltip(
            uiText("When disabled, emotes that overlap other emotes, such as BTTV's "
            "cvMask and 7TV's RainTime, will appear as normal emotes."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Autocompletion"));
    SettingWidget::checkbox(uiText("Enable emote completion by typing :"),
                            s.emoteCompletionWithColon)
        ->setTooltip(
            uiText("With this setting enabled, typing the colon character opens the "
            "colon-completion popup which gives you an updating list of emotes "
            "matching the text after the colon."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Use experimental smarter emote completion."),
                            s.useSmartEmoteCompletion)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Emotes and badges"),
                 QStringLiteral("Emotes"));
    layout.addDropdown<float>(
        uiText("Size"), {"0.5x", "0.75x", "Default", "1.25x", "1.5x", "2x"},
        s.emoteScale,
        [](auto val) {
            if (val == 1)
            {
                return QString("Default");
            }
            else
            {
                return QString::number(val) + "x";
            }
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 1.f);
        });

    SettingWidget::checkbox(uiText("Remove spaces between emotes"),
                            s.removeSpacesBetweenEmotes)
        ->setTooltip(uiText("When enabled, adjacent emotes will no longer have an "
                     "added space separating them."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show unlisted 7TV emotes"),
                            s.showUnlistedSevenTVEmotes)
        ->addKeywords({"seventv"})
        ->addTo(layout);

    s.showUnlistedSevenTVEmotes.connect(
        []() {
            getApp()->getTwitch()->forEachChannelAndSpecialChannels(
                [](const auto &c) {
                    if (c->isTwitchChannel())
                    {
                        auto *channel = dynamic_cast<TwitchChannel *>(c.get());
                        if (channel != nullptr)
                        {
                            channel->refreshSevenTVChannelEmotes(false);
                        }
                    }
                });
        },
        false);

    SettingWidget::dropdown(uiText("Show emote & badge thumbnail on hover"),
                            s.emotesTooltipPreview)
        ->addTo(layout);

    SettingWidget::dropdown(uiText("Emote & badge thumbnail size on hover"),
                            s.emoteTooltipScale)
        ->addTo(layout);

    SettingWidget::dropdown(uiText("Emoji style"), s.emojiSet)->addTo(layout);

    routeSetting(layout, QStringLiteral("Emotes and badges"),
                 QStringLiteral("Emote providers"));
    SettingWidget::checkbox(uiText("Show BetterTTV global emotes"),
                            s.enableBTTVGlobalEmotes)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show BetterTTV channel emotes"),
                            s.enableBTTVChannelEmotes)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox(
        uiText("Enable BetterTTV live emote updates (requires restart)"),
        s.enableBTTVLiveUpdates)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Send activity to BetterTTV"), s.sendBTTVActivity)
        ->setTooltip(
            uiText("When enabled, Leafyrino will signal an activity to BetterTTV "
            "when you send a chat message. This is used for badges, "
            " and personal emotes. When disabled, no activity "
            "is sent and others won't see your cosmetics."))
        ->addKeywords({"bttv"})
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show FrankerFaceZ global emotes"),
                            s.enableFFZGlobalEmotes)
        ->addKeywords({"ffz"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show FrankerFaceZ channel emotes"),
                            s.enableFFZChannelEmotes)
        ->addKeywords({"ffz"})
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show 7TV global emotes"),
                            s.enableSevenTVGlobalEmotes)
        ->addKeywords({"seventv"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show 7TV channel emotes"),
                            s.enableSevenTVChannelEmotes)
        ->addKeywords({"seventv"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show 7TV channel emotes"),
                            s.enableSevenTVPersonalEmotes)
        ->addKeywords({"seventv"})
        ->setTooltip(uiText("This requires '7TV live updates' to work."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Enable 7TV live emote updates (requires restart)"),
                            s.enableSevenTVEventAPI)
        ->addKeywords({"seventv"})
        ->setTooltip(uiText("When enabled, channel emotes will get updated "
                     "automatically (no reload required) and cosmetics "
                     "(badges/paints/personal emotes) will get updated."))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Send activity to 7TV"), s.sendSevenTVActivity)
        ->setTooltip(uiText("When enabled, Leafyrino will signal an activity to 7TV "
                     "when you send a chat message. This is used for badges, "
                     "paints, and personal emotes. When disabled, no activity "
                     "is sent and others won't see your cosmetics."))
        ->addKeywords({"seventv"})
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Streamer Mode"),
                 QStringLiteral("Streamer Mode"));
    layout.addDescription(
        uiText("Leafyrino can automatically change behavior if it detects that any "
        "streaming software is running.\nSelect which things you want to "
        "change while streaming"));

    SettingWidget::dropdown(uiText("Enable Streamer Mode"), s.enableStreamerMode)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide usercard avatars"),
                            s.streamerModeHideUsercardAvatars)
        ->setTooltip(uiText("Prevent potentially explicit avatars from showing."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide link thumbnails"),
                            s.streamerModeHideLinkThumbnails)
        ->setTooltip(uiText("Prevent potentially explicit thumbnails from showing "
                     "when hovering links."))
        ->addTo(layout);

    SettingWidget::checkbox(
        uiText("Hide viewer count and stream length while hovering over split header"),
        s.streamerModeHideViewerCountAndDuration)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide moderation actions"),
                            s.streamerModeHideModActions)
        ->setTooltip(
            uiText("Hide bans, timeouts, and automod messages from appearing in chat."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide messages from restricted users"),
                            s.streamerModeHideRestrictedUsers)
        ->setTooltip(uiText("Restricted users can be marked by you, your moderators, "
                     "or Twitch's AutoMod"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide blocked terms"),
                            s.streamerModeHideBlockedTermText)
        ->setTooltip(
            uiText("Hide blocked terms from showing up in places like AutoMod "
            "messages. This can be useful in case you have some blocked terms "
            "that you don't want to show on stream."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Mute mention sounds"), s.streamerModeMuteMentions)
        ->setTooltip(uiText("Mute your ping sound from playing."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Suppress Live Notifications"),
                            s.streamerModeSuppressLiveNotifications)
        ->setTooltip(
            uiText("Hide Live notification popups from appearing. (Windows Only)"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Suppress Inline Whispers"),
                            s.streamerModeSuppressInlineWhispers)
        ->setTooltip(uiText("Hide whispers sent to you from appearing in chat."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Links and search"),
                 QStringLiteral("Link Previews"));
    layout.addDescription(
        uiText("Extra information like \"youtube video stats\" or title of webpages "
        "can be loaded for all links if enabled. Optionally you can also show "
        "thumbnails for emotes, videos and more. The information is pulled "
        "from our servers. The Link Previews are loaded through <a "
        "href=\"https://github.com/Chatterino/api\">an API</a> hosted by the "
        "Chatterino developers. These are the API <a "
        "href=\"https://braize.pajlada.com/chatterino/legal/"
        "terms-of-service\">Terms of Services</a> and <a "
        "href=\"https://braize.pajlada.com/chatterino/legal/"
        "privacy-policy\">Privacy Policy</a>."));

    SettingWidget::checkbox(uiText("Enable"), s.linkInfoTooltip)->addTo(layout);
    SettingWidget::checkbox(uiText("Show page title instead of the link"),
                            s.showLinkTitles)
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Show a card under Twitch clip links"),
                            s.showClipCards)
        ->addTo(layout);

    layout.addDropdown<int>(
        uiText("Also show thumbnails if available"),
        {"Off", "Small", "Medium", "Large"}, s.thumbnailSize,
        [](auto val) {
            if (val == 0)
            {
                return QString("Off");
            }
            else if (val == 100)
            {
                return QString("Small");
            }
            else if (val == 200)
            {
                return QString("Medium");
            }
            else if (val == 300)
            {
                return QString("Large");
            }
            else
            {
                return QString::number(val);
            }
        },
        [](auto args) {
            if (args.value == "Small")
            {
                return 100;
            }
            else if (args.value == "Medium")
            {
                return 200;
            }
            else if (args.value == "Large")
            {
                return 300;
            }

            return fuzzyToInt(args.value, 0);
        });
    layout.addDropdown<int>(
        uiText("Show thumbnails of streams"), {"Off", "Small", "Medium", "Large"},
        s.thumbnailSizeStream,
        [](auto val) {
            if (val == 0)
            {
                return QString("Off");
            }
            else if (val == 1)
            {
                return QString("Small");
            }
            else if (val == 2)
            {
                return QString("Medium");
            }
            else if (val == 3)
            {
                return QString("Large");
            }
            else
            {
                return QString::number(val);
            }
        },
        [](auto args) {
            if (args.value == "Small")
            {
                return 1;
            }
            else if (args.value == "Medium")
            {
                return 2;
            }
            else if (args.value == "Large")
            {
                return 3;
            }

            return fuzzyToInt(args.value, 0);
        });

    layout.addNavigationSpacing();
    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Startup and tray"));
    layout.addButton(uiText("Getting started"), [this] {
        showWelcomeDialog(this->window());
    });

    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Updates"));
    if (Version::instance().isSupportedOS())
    {
        layout.addDescription(
            uiText("The program checks for a new version by itself at startup "
            "and every 6 hours. When one is out, a card appears at the top "
            "of the window: click it to update. Releases and the list of "
            "changes: <a href='https://github.com/Mist10X/chatterino-by-mist1x/"
            "releases'>GitHub</a>."));

        auto *status = new QLabel;
        status->setWordWrap(true);
        const auto showStatus = [status](Updates::Status st) {
            auto &updates = getApp()->getUpdates();
            switch (st)
            {
                case Updates::Searching:
                    status->setText(uiText("Checking…"));
                    break;
                case Updates::UpdateAvailable:
                    status->setText(uiText("Update %1 is available")
                                        .arg(updates.getOnlineVersion()));
                    break;
                case Updates::NoUpdateAvailable:
                    status->setText(uiText("You have the newest version (%1).")
                                        .arg(updates.getCurrentVersion()));
                    break;
                case Updates::SearchFailed:
                    status->setText(uiText("Could not reach GitHub. Check "
                                           "the internet connection."));
                    break;
                case Updates::Downloading:
                    status->setText(uiText("Downloading the update…"));
                    break;
                default:
                    status->setText(uiText("Your version: %1")
                                        .arg(updates.getCurrentVersion()));
                    break;
            }
        };
        showStatus(getApp()->getUpdates().getStatus());
        this->managedConnections_.managedConnect(
            getApp()->getUpdates().statusUpdated, showStatus);

        auto *check = layout.makeButton(uiText("Check for updates now"), [] {
            auto &updates = getApp()->getUpdates();
            if (updates.getStatus() == Updates::UpdateAvailable)
            {
                showUpdatePrompt(nullptr);
                return;
            }
            updates.checkForUpdates();
        });
        auto *row = new QHBoxLayout;
        row->addWidget(check);
        row->addWidget(status, 1);
        layout.addLayout(row);

        SettingWidget::checkbox(uiText("Also offer test versions"),
                                s.betaUpdates)
            ->addTo(layout);
    }
    else
    {
        layout.addDescription(
            uiText("Your operating system is not officially supplied with builds. For "
            "updates, please rebuild Chatterino from sources. Report "
            "issues <a href='https://chatterino.com/link/issues'>here</a>."));
    }

    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("AppData & Cache"));

    layout.addSubtitle(uiText("Application Data"));
    layout.addDescription(uiText("All local files like settings and cache files are "
                          "stored in this directory."));
    layout.addButton(uiText("Open AppData directory"), [] {
#ifdef Q_OS_DARWIN
        QDesktopServices::openUrl("file://" +
                                  getApp()->getPaths().rootAppDataDirectory);
#else
        QDesktopServices::openUrl(getApp()->getPaths().rootAppDataDirectory);
#endif
    });

    layout.addSubtitle(uiText("Temporary files (Cache)"));
    layout.addDescription(
        uiText("Files that are used often (such as emotes) are saved to disk to "
        "reduce bandwidth usage and to speed up loading."));

    auto *cachePathLabel = layout.addDescription(uiText("placeholder :D"));
    getSettings()->cachePath.connect([cachePathLabel](const auto &,
                                                      auto) mutable {
        QString newPath = getApp()->getPaths().cacheDirectory();

        QString pathShortened = uiText("Cache saved at <a href=\"file:///") + newPath +
                                R"("><span style="color: white;">)" +
                                shortenString(newPath, 50) + "</span></a>";
        cachePathLabel->setText(pathShortened);
        cachePathLabel->setToolTip(newPath);
    });

    {
        auto *box = new QHBoxLayout;

        box->addWidget(layout.makeButton("Choose cache path", [this]() {
            const auto chosen = QFileDialog::getExistingDirectory(this);
            if (chosen.isEmpty())
            {
                return;
            }
            // Its own folder inside the chosen one: clearing the cache must
            // never touch what else is there.
            getSettings()->cachePath = QDir(chosen).filePath(
                QStringLiteral("Chatterino by Mist1X cache"));
        }));
        box->addWidget(layout.makeButton("Reset", []() {
            getSettings()->cachePath = "";
        }));
        box->addWidget(layout.makeButton("Clear Cache", [&layout]() {
            auto reply = QMessageBox::question(
                layout.window(), uiText("Clear cache"),
                uiText("Are you sure that you want to clear your cache? Emotes may "
                "take longer to load next time Chatterino is started."),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes)
            {
                const auto path = getApp()->getPaths().cacheDirectory();
                if (!isSafeCacheFolder(path))
                {
                    QMessageBox::warning(
                        layout.window(), uiText("Clear cache"),
                        uiText("This folder is not only a cache, so it is not "
                               "cleared. Choose a cache folder again or reset "
                               "it."));
                    return;
                }
                auto cacheDir = QDir(path);
                cacheDir.removeRecursively();
                QDir().mkpath(path);
            }
        }));
        box->addStretch(1);

        layout.addLayout(box);
    }

    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Sound"));

    SettingWidget::dropdown(uiText("Sound backend (requires restart)"), s.soundBackend)
        ->setTooltip(uiText("Change this only if you're noticing issues with sound "
                     "playback on your system"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Keep sound backend alive (requires restart)"),
                            s.soundMiniaudioKeepEngineAlive)
        ->setTooltip(
            uiText("This setting makes Chatterino output silence to your sound "
            "device, even if no ping is being played. Try this setting if you "
            "have issues with wireless devices not playing the first sound. "
            "Note that this can prevent your monitor or computer from "
            "sleeping."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Chat header"));

    layout.addDescription(uiText("In live channels show:"));
    SettingWidget::checkbox(uiText("Uptime"), s.headerUptime)
        ->setTooltip(uiText("Show how long the channel has been live"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Viewer count"), s.headerViewerCount)
        ->setTooltip(uiText("Show how many users are watching"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Category"), s.headerGame)
        ->setTooltip(uiText("Show what Category the stream is listed under"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Title"), s.headerStreamTitle)
        ->setTooltip(uiText("Show the stream title"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Unique chat (R9K)"));
    auto toggleLocalr9kSeq = getApp()->getHotkeys()->getDisplaySequence(
        HotkeyCategory::Window, "toggleLocalR9K");
    QString toggleLocalr9kShortcut =
        "an assigned hotkey (Window -> Toggle local unique chat (R9K))";
    if (!toggleLocalr9kSeq.isEmpty())
    {
        toggleLocalr9kShortcut = toggleLocalr9kSeq.toString(
            QKeySequence::SequenceFormat::NativeText);
    }
    layout.addDescription(uiText("Hide similar messages to those previously seen. "
                          "Toggle hidden messages by pressing ") +
                          toggleLocalr9kShortcut + ".");

    SettingWidget::checkbox(uiText("Enable similarity checks"), s.similarityEnabled)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Only if by the same user"), s.hideSimilarBySameUser)
        ->setTooltip(
            uiText("When checked, messages that are very similar to each other can "
            "still be shown as long as they're sent by different users."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Hide my own messages"), s.hideSimilarMyself)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Receive notification sounds from hidden messages"),
                            s.shownSimilarTriggerHighlights)
        ->addTo(layout);

    s.hideSimilar.connect(
        []() {
            getApp()->getWindows()->forceLayoutChannelViews();
        },
        false);
    layout.addDropdown<float>(
        uiText("Similarity threshold"), {"0.5", "0.75", "0.9"}, s.similarityPercentage,
        [](auto val) {
            return QString::number(val);
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 0.9F);
        },
        true,
        "A value of 0.9 means the messages need to be 90% similar to be marked "
        "as similar.");
    layout.addDropdown<int>(
        uiText("Maximum delay between messages"),
        {"5s", "10s", "15s", "30s", "60s", "120s"}, s.hideSimilarMaxDelay,
        [](auto val) {
            return QString::number(val) + "s";
        },
        [](auto args) {
            return fuzzyToInt(args.value, 5);
        },
        true,
        "A value of 5s means if there's a 5s break between messages, we will "
        "stop looking further through the messages for similarities.");
    layout.addDropdown<int>(
        uiText("Amount of previous messages to check"), {"1", "2", "3", "4", "5"},
        s.hideSimilarMaxMessagesToCheck,
        [](auto val) {
            return QString::number(val);
        },
        [](auto args) {
            return fuzzyToInt(args.value, 3);
        },
        true,
        "How many messages in the history should be compared to a new one to "
        "establish its similarity rating. Messages in the history will be "
        "compared to only if they are new enough.");

    routeSetting(layout, QStringLiteral("Emotes and badges"),
                 QStringLiteral("Visible badges"));
    SettingWidget::checkbox(uiText("Authority"), s.showBadgesGlobalAuthority)
        ->setTooltip(uiText("e.g. staff, admin"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Predictions"), s.showBadgesPredictions)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Channel"), s.showBadgesChannelAuthority)
        ->setTooltip(uiText("e.g. broadcaster, moderator"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Subscriber "), s.showBadgesSubscription)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Vanity"), s.showBadgesVanity)
        ->setTooltip(uiText("e.g. prime, bits, sub gifter"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Chatterino"), s.showBadgesChatterino)
        ->setTooltip(uiText("e.g. Chatterino Supporter/Contributor/Developer"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("FrankerFaceZ"), s.showBadgesFfz)
        ->addKeywords({"ffz"})
        ->setTooltip(uiText("e.g. Bot, FrankerFaceZ supporter, FrankerFaceZ developer"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("7TV"), s.showBadgesSevenTV)
        ->addKeywords({"seventv"})
        ->setTooltip(uiText("Badges for 7TV admins, developers, and supporters"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("BetterTTV"), s.showBadgesBttv)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Homies Supporter"), s.showBadgesHomiesSupporter)
        ->addKeywords({"homies", "supporter"})
        ->setTooltip(uiText("Badges from the Homies supporter badge list"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Homies Custom"), s.showBadgesHomiesCustom)
        ->addKeywords({"homies", "custom"})
        ->setTooltip(uiText("Custom Homies badges"))
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Moltorino"), s.showBadgesMoltorino)
        ->addKeywords({"moltorino", "supporter", "top donor", "developer"})
        ->setTooltip(
            uiText("Badges for Moltorino supporters, top donors, and developers"))
        ->addTo(layout);
    layout.addSeparator();
    SettingWidget::checkbox(uiText("Use custom FrankerFaceZ moderator badges"),
                            s.useCustomFfzModeratorBadges)
        ->addKeywords({"ffz"})
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Use custom FrankerFaceZ VIP badges"),
                            s.useCustomFfzVipBadges)
        ->addKeywords({"ffz"})
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Animate 7TV badges"), s.animateSevenTVBadges)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Overlay"));
    layout.addDropdown<float>(
        uiText("Zoom factor"), ZOOM_LEVELS, s.overlayScaleFactor,
        [](auto val) {
            if (val == 1)
            {
                return u"Default"_s;
            }
            return QString::number(val) + 'x';
        },
        [](const auto &args) {
            return fuzzyToFloat(args.value, 1.F);
        },
        true,
        "The final scale of the messages in the overlay is computed by "
        "multiplying this zoom factor with the global zoom level.");

    SettingWidget::intInput(uiText("Background opacity (0-255)"),
                            s.overlayBackgroundOpacity,
                            {
                                .min = 0,
                                .max = 255,
                                .singleStep = 1,
                            })
        ->setTooltip(
            uiText("Controls the opacity of the (possibly alternating) background "
            "behind messages. The color is set through the current theme. 255 "
            "corresponds to a fully opaque background."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Enable Shadow"), s.enableOverlayShadow)
        ->setTooltip(uiText("Enables a drop shadow on the overlay. This will use more "
                     "processing power."))
        ->addTo(layout);

    SettingWidget::intInput(uiText("Shadow opacity (0-255)"), s.overlayShadowOpacity,
                            {
                                .min = 0,
                                .max = 255,
                                .singleStep = 1,
                            })
        ->setTooltip(uiText("Controls the opacity of the added drop shadow. 255 "
                     "corresponds to a fully opaque shadow."))
        ->addTo(layout);

    SettingWidget::colorButton(uiText("Shadow color"), s.overlayShadowColor)
        ->addTo(layout);

    SettingWidget::intInput(uiText("Shadow radius"), s.overlayShadowRadius,
                            {
                                .min = 0,
                                .max = 40,
                                .singleStep = 1,
                                .suffix = "dp",
                            })
        ->setTooltip(uiText("Controls how far the shadow is spread (the blur "
                     "radius) in device-independent pixels."))
        ->addTo(layout);

    SettingWidget::intInput(uiText("Shadow offset x"), s.overlayShadowOffsetX,
                            {
                                .min = -20,
                                .max = 20,
                                .singleStep = 1,
                                .suffix = "dp",
                            })
        ->setTooltip(uiText("Controls how far the shadow is offset on the x axis in "
                     "device-independent pixels. A negative value offsets to "
                     "the left and a positive to the right."))
        ->addTo(layout);

    SettingWidget::intInput(uiText("Shadow offset y"), s.overlayShadowOffsetY,
                            {
                                .min = -20,
                                .max = 20,
                                .singleStep = 1,
                                .suffix = "dp",
                            })
        ->setTooltip(uiText("Controls how far the shadow is offset on the y axis in "
                     "device-independent pixels. A negative value offsets to "
                     "the top and a positive to the bottom."))
        ->addTo(layout);

    {
        routeSetting(layout, QStringLiteral("Links and search"),
                     QStringLiteral("Search"));
        layout.addDescription(
            uiText("Search engine which appears when you select text and right-click "
            "a message. Select a search engine preset from the dropdown below, "
            "or fill in your custom search engine URL and name."));
        SettingWidget::checkbox(uiText("Enable search in right-click context menu"),
                                s.searchEnabled)
            ->setTooltip(
                uiText("Allow searching selected text using a search engine from "
                "the right-click context menu."))
            ->addTo(layout);

        QStringList presetList = {"DuckDuckGo", "Bing", "Google"};
        auto *presetCombo =
            layout.addDropdown(uiText("Search engine preset"), presetList,
                               "Select a search engine preset");
        presetCombo->setPlaceholderText(uiText("Select..."));
        presetCombo->setCurrentIndex(-1);

        QPalette palette = presetCombo->palette();
        palette.setColor(QPalette::PlaceholderText, QColor(255, 255, 255));
        presetCombo->setPalette(palette);
        s.searchEnabled.connect([presetCombo](bool value) {
            presetCombo->setEnabled(value);
        });

        QObject::connect(
            presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [&s, presetCombo](int index) {
                if (index < 0)
                    return;

                QString preset = presetCombo->itemText(index);
                if (preset == "DuckDuckGo")
                {
                    s.searchEngineUrl = "https://duckduckgo.com/?q=";
                    s.searchEngineName = "DuckDuckGo";
                }
                else if (preset == "Bing")
                {
                    s.searchEngineUrl = "https://www.bing.com/search?q=";
                    s.searchEngineName = "Bing";
                }
                else if (preset == "Google")
                {
                    s.searchEngineUrl = "https://www.google.com/search?q=";
                    s.searchEngineName = "Google";
                }

                {
                    QSignalBlocker blocker(presetCombo);
                    presetCombo->setCurrentIndex(-1);
                }
            });

        SettingWidget::lineEdit(uiText("Search engine URL"), s.searchEngineUrl)
            ->conditionallyEnabledBy(s.searchEnabled)
            ->addTo(layout);

        SettingWidget::lineEdit(uiText("Search engine name"), s.searchEngineName)
            ->conditionallyEnabledBy(s.searchEnabled)
            ->addTo(layout);
    }
    if (supportsIncognitoLinks())
    {
        SettingWidget::checkbox(uiText("Search in incognito/private mode"),
                                s.searchIncognito)
            ->addTo(layout);
    }

    if (supportsIncognitoLinks())
    {
        routeSetting(layout, QStringLiteral("Links and search"),
                     QStringLiteral("Links"));
        SettingWidget::checkbox(uiText("Open links in incognito/private mode"),
                                s.openLinksIncognito)
            ->addTo(layout);
    }

    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Startup and tray"));
    SettingWidget::customCheckbox(
        uiText("Restart on crash (requires restart)"),
        getApp()->getCrashHandler()->shouldRecover(),
        [](bool on) {
            getApp()->getCrashHandler()->saveShouldRecover(on);
        })
        ->setTooltip(uiText("When possible, restart Chatterino if the program crashes"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("User cards"),
                 QStringLiteral("Usercard"));
    SettingWidget::checkbox(uiText("Show 7TV Animated Profile Picture"),
                            s.displaySevenTVAnimatedProfile)
        ->addTo(layout);
    routeSetting(layout, QStringLiteral("Emotes and badges"),
                 QStringLiteral("Emotes"));
    SettingWidget::checkbox(uiText("Load AVIF images"), s.allowAvifImages)
        ->setTooltip(
            uiText("When enabled and an AVIF decoder is found, AVIF images will be "
            "preferred over WEBP on 7TV. This saves bandwidth."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("What to show in chat"));
    SettingWidget::inverseCheckbox(uiText("Show moderation messages"),
                                   s.hideModerationActions)
        ->setTooltip(
            uiText("Show messages for timeouts, bans, and other moderator actions."))
        ->addTo(layout);

    SettingWidget::inverseCheckbox(uiText("Show deletions of single messages"),
                                   s.hideDeletionActions)
        ->setTooltip(uiText("Show when a single message is deleted.\ne.g. A message "
                     "from TreuKS was deleted: abc"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Usernames"));
    SettingWidget::checkbox(uiText("Colorize users without color set (gray names)"),
                            s.colorizeNicknames)
        ->setTooltip(uiText("Grant a random color to users who never set a color for "
                     "themselves"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Autocompletion"));
    SettingWidget::checkbox(uiText("Mention users with a comma"),
                            s.mentionUsersWithComma)
        ->setTooltip(uiText("When using tab-completion, if the username is at the "
                     "start of the message, include a comma at the end of the "
                     "name.\ne.g. pajl -> pajlada,"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("What to show in chat"));
    SettingWidget::checkbox(uiText("Show joined users (< 1000 chatters)"), s.showJoins)
        ->setTooltip(
            uiText("Show a Twitch system message stating what users have joined the "
            "chat, only available when the chat has less than 1000 users"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show parted users (< 1000 chatters)"), s.showParts)
        ->setTooltip(
            uiText("Show a Twitch system message stating what users have left the "
            "chat, only available when chat has less than 1000 users"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("User cards"),
                 QStringLiteral("Usercard"));
    SettingWidget::checkbox(
        uiText("Automatically close user popup when it loses focus"),
        s.autoCloseUserPopup)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Reply threads"));
    SettingWidget::checkbox(
        uiText("Automatically close reply thread popup when it loses focus"),
        s.autoCloseThreadPopup)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Emotes and badges"),
                 QStringLiteral("7TV paints"));
    SettingWidget::checkbox(uiText("Display 7TV Paints"), s.displaySevenTVPaints)
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Display 7TV Paint Shadows"),
                            s.displaySevenTVPaintShadows)
        ->addTo(layout);
    SettingWidget::checkbox(uiText("Use larger 7TV Paint Shadows"),
                            s.largeSevenTVPaintShadows)
        ->setDescription(
            uiText("This aims to match the appearance of paints in the browser."))
        ->addTo(layout);
    {
        auto cb = [] {
            getApp()->getWindows()->invalidateChannelViewBuffers();
        };
        s.displaySevenTVPaints.connect(cb, false);
        s.displaySevenTVPaintShadows.connect(cb, false);
        s.largeSevenTVPaintShadows.connect(cb, false);
    }

    routeSetting(layout, QStringLiteral("Links and search"),
                 QStringLiteral("Links"));
    SettingWidget::checkbox(uiText("Lowercase domains (anti-phishing)"),
                            s.lowercaseDomains)
        ->setTooltip(
            uiText("Make all clickable links lowercase to deter phishing attempts."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("User cards"),
                 QStringLiteral("Usercard"));
    SettingWidget::checkbox(uiText("Show user's pronouns in user card"), s.showPronouns)
        ->setDescription(
            uiText(R"(Pronouns are retrieved from <a href="https://pr.alejo.io">pr.alejo.io</a> when a user card is opened.)"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("What to show in chat"));
    SettingWidget::checkbox(uiText("Show stream title in live message"),
                            s.showTitleInLiveMessage)
        ->setTooltip(uiText("The title in the message will be the title the streamer "
                     "set when they went live, and will not update as the "
                     "streamer updates their title."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Usernames"));
    SettingWidget::checkbox(uiText("Bold @usernames"), s.boldUsernames)
        ->setTooltip(uiText("Bold @mentions to make them more noticeable."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Color @usernames"), s.colorUsernames)
        ->setTooltip(uiText("If Chatterino has seen a user, highlight @mention's of "
                     "them with their Twitch color."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Try to find usernames without @ prefix"),
                            s.findAllUsernames)
        ->setTooltip(uiText("Find mentions of users in chat without the @ prefix."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Autocompletion"));
    SettingWidget::checkbox(uiText("Show username autocompletion popup menu"),
                            s.showUsernameCompletionMenu)
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Always include broadcaster in user completions"),
                            s.alwaysIncludeBroadcasterInUserCompletions)
        ->setTooltip(
            uiText("This will ensure a broadcaster is always easy to ping, even if "
            "they don't have chat open or have typed recently."))
        ->addTo(layout);

    const QStringList usernameDisplayModes = {"Username", "Localized name",
                                              "Username and localized name"};

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Usernames"));
    ComboBox *nameDropdown =
        layout.addDropdown<std::underlying_type_t<UsernameDisplayMode>>(
            uiText("Username style"), usernameDisplayModes, s.usernameDisplayMode,
            [usernameDisplayModes](auto val) {
                return usernameDisplayModes.at(val - 1);
            },
            [](auto args) {
                return args.index + 1;
            },
            false,
            "Customizes how you see Asian Language names.\nUsing an option "
            "that includes \"localized\" will display the username in its "
            "respective Asian language.\ne.g. "
            "Username and localized: testaccount_420(테스트계정420)\n"
            "Username: testaccount_420\n"
            "Localized name: 테스트계정420");
    nameDropdown->setMinimumWidth(nameDropdown->minimumSizeHint().width());

    layout.addDropdown<float>(
        uiText("Username font weight"), {"50", "Default", "75", "100"}, s.boldScale,
        [](auto val) {
            if (val == 63)
            {
                return QString("Default");
            }
            else
            {
                return QString::number(val);
            }
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 63.f);
        });

    routeSetting(layout, QStringLiteral("Links and search"),
                 QStringLiteral("Links"));
    SettingWidget::checkbox(
        uiText("Double click to open links and other elements in chat"),
        s.linksDoubleClickOnly)
        ->setTooltip(uiText("When enabled, opening links/usercards requires "
                     "double-clicking.\nUseful for making sure you don't "
                     "accidentally click on suspicious links.\nClicking a link "
                     "once will pause the chat briefly to allow for a less "
                     "accident-prone double-clicking."))
        ->addKeywords({"pause"})
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Unshorten links"), s.unshortLinks)
        ->setTooltip(uiText("When enabled, \"right-click + copy link\" will copy the "
                     "unshortened version of the link.\ne.g. "
                     "https://bit.ly/mrfors -> https://forsen.tv/"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Autocompletion"));
    SettingWidget::checkbox(
        uiText("Only search for emote autocompletion at the start of emote names"),
        s.prefixOnlyEmoteCompletion)
        ->setTooltip(uiText("When disabled, emote tab-completion will complete based "
                     "on any part of the name.\ne.g. sheffy -> DatSheffy"))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Only search for username autocompletion with an @"),
                            s.userCompletionOnlyWithAt)
        ->setTooltip(uiText("When enabled, username tab-completion will only complete "
                     "when using @\ne.g. pajl -> pajl | @pajl -> @pajlada"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Mentions and whispers"));
    SettingWidget::checkbox(uiText("Show Twitch whispers inline"), s.inlineWhispers)
        ->setTooltip(uiText("Show whispers as messages in all splits instead of just "
                     "/whispers."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Highlight received inline whispers"),
                            s.highlightInlineWhispers)
        ->setTooltip(
            uiText("Highlight the whispers shown in all splits.\nIf \"Show Twitch "
            "whispers inline\" is disabled, this setting will do nothing."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Reply threads"));
    SettingWidget::checkbox(
        uiText("Automatically subscribe to participated reply threads"),
        s.autoSubToParticipatedThreads)
        ->setTooltip(
            uiText("When enabled, you will automatically subscribe to reply threads "
            "you participate in.\nThis means reply threads you participate in "
            "will use your \"Subscribed Reply Threads\" highlight settings."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Message history"));
    SettingWidget::checkbox(uiText("Load message history on connect"),
                            s.loadTwitchMessageHistoryOnConnect)
        ->addTo(layout);

    SettingWidget::dropdown(uiText("Message history provider"), s.recentMessagesApi)
        ->setTooltip(
            uiText("Service used to load chat history when joining a channel.\n"
            "CHATTERINO2_RECENT_MESSAGES_URL overrides this when set."))
        ->addTo(layout);

    // TODO: Change phrasing to use better english once we can tag settings, right now it's kept as history instead of historical so that the setting shows up when the user searches for history
    SettingWidget::intInput(uiText("Max number of history messages to load on connect"),
                            s.twitchMessageHistoryLimit,
                            {
                                .min = 10,
                                .max = 10000,
                                .singleStep = 10,
                            })
        ->addTo(layout);

    SettingWidget::intInput(uiText("Split message scrollback limit (requires restart)"),
                            s.scrollbackSplitLimit,
                            {
                                .min = 100,
                                .max = 100000,
                                .singleStep = 100,
                            })
        ->setTooltip(archiveConnected()
                         ? uiText("When automatic chat cleanup is enabled in Archive, all chats grow until its hourly cleanup. Selected channels are exempt. Saved Archive data is not deleted.")
                         : uiText("How many messages a chat keeps; older ones are removed from the chat."))
        ->addTo(layout);

    SettingWidget::intInput(uiText("Usercard scrollback limit (requires restart)"),
                            s.scrollbackUsercardLimit,
                            {
                                .min = 100,
                                .max = 100000,
                                .singleStep = 100,
                            })
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Moderation tools"),
                 QStringLiteral("Moderation"));
    SettingWidget::dropdown(uiText("Show blocked term automod messages"),
                            s.showBlockedTermAutomodMessages)
        ->setTooltip(uiText("Show messages that are blocked by AutoMod for containing "
                     "a public blocked term in the current channel."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("What to show in chat"));
    layout.addDropdown<int>(
        uiText("Stack timeouts"), {"Stack", "Stack until timeout", "Don't stack"},
        s.timeoutStackStyle,
        [](int index) {
            return index;
        },
        [](auto args) {
            return args.index;
        },
        false, "Combine consecutive timeout messages into a single message.");

    SettingWidget::checkbox(uiText("Combine multiple bit tips into one"), s.stackBits)
        ->setTooltip(uiText("Combine consecutive cheermotes (sent in a single "
                     "message) into one cheermote."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Mentions and whispers"));
    SettingWidget::checkbox(uiText("Messages in /mentions highlights tab"),
                            s.highlightMentions)
        ->setTooltip(uiText("When disabled, the /mentions tab will not highlight in "
                     "red when you are mentioned."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Reply threads"));
    SettingWidget::checkbox(uiText("Strip leading mention in replies"),
                            s.stripReplyMention)
        ->setTooltip(
            uiText("When disabled, messages sent in reply threads will include the "
            "@mention for the related thread. If the reply context is hidden, "
            "these mentions will never be stripped."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Sending"));
    SettingWidget::dropdown(uiText("Chat send protocol"), s.chatSendProtocol)
        ->setTooltip(uiText("'Helix' will use Twitch's Helix API to send messages. "
                     "'IRC' will use IRC to send messages."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat input"),
                 QStringLiteral("Input Box"));
    SettingWidget::checkbox(uiText("Show send message button"), s.showSendButton)
        ->setTooltip(uiText("Show a Send button next to each split input that can be "
                     "clicked to send the message"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Experimental"));
    SettingWidget::checkbox(
        uiText("Enable experimental Twitch EventSub support (requires restart)"),
        s.enableExperimentalEventSub)
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Appearance"),
                 QStringLiteral("Tabs"));
    SettingWidget::checkbox(uiText("Disable renaming of tabs on double-click"),
                            s.disableTabRenamingOnClick)
        ->setTooltip(uiText("Prevents the rename dialog from opening when a tab is "
                     "double-clicked"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("Chat"),
                 QStringLiteral("Shared chat"));
    SettingWidget::intInput(
        uiText("Shared chat session status refresh interval"),
        s.sharedChatSessionRefreshInterval,
        {.min = 5, .max = 999, .singleStep = 1, .suffix = "s"})
        ->setTooltip(uiText("How often Chatterino polls the Twitch API for the "
                     "shared chat session status."))
        ->addTo(layout);

    SettingWidget::checkbox(uiText("Show shared chat badge for all messages"),
                            s.sharedChatAlwaysShowBadge)
        ->setTooltip(
            uiText("If turned off, only messages from other participants have a "
            "shared chat badge"))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("System settings"),
                 QStringLiteral("Connection"));
    SettingWidget::dropdown(uiText("Twitch read connection mode (requires restart)"),
                            s.twitchReadConnectionMode)
        ->setTooltip(uiText("The read connection is the one where Chatterino joins a "
                     "channel and listens to the messages.\n"
                     "- Authenticated: Join as your logged in user.\n"
                     "- Anonymous: Join as an anonymous user. This causes to "
                     "you not show up in the viewer list.\n"
                     "- Anonymous (parallel): Join as an anonymous user on "
                     "multiple connections at once. This speeds up the "
                     "connection phase when joining many channels. The other "
                     "modes will join in delayed batches."))
        ->addTo(layout);

    routeSetting(layout, QStringLiteral("YouTube and Kick"),
                 QStringLiteral("Kick"));
    SettingWidget::dropdown(uiText("Kick connection preference (requires restart)"),
                            s.kickConnectionPreference)
        ->setTooltip(uiText("The transport to use for receiving Kick messages.\n"
                     "- Default: Use Pusher.\n"
                     "- Pusher: Use Kick's Pusher app. This was historically "
                     "the default, but the web app has moved on.\n"
                     "- Centrifugo: Use Kick's centrifugo instance. This is "
                     "usually used by default on the web.\n"
                     "- Any: Advertise support for both Pusher and Centrifugo. "
                     "This matches the behaviour on the web.\n"))
        ->addTo(layout);

    layout.addStretch();

    auto *inv = new BaseWidget(this);

    layout.addWidget(inv);
}

void GeneralPage::initExtra()
{
    if (this->cachePath_)
    {
        getSettings()->cachePath.connect(
            [cachePath = this->cachePath_](const auto &, auto) mutable {
                QString newPath = getApp()->getPaths().cacheDirectory();

                QString pathShortened = "Current location: <a href=\"file:///" +
                                        newPath + "\">" +
                                        shortenString(newPath, 50) + "</a>";

                cachePath->setText(pathShortened);
                cachePath->setToolTip(newPath);
            });
    }
}

}  // namespace chatterino
