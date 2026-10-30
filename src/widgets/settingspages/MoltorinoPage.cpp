#include "common/UiLanguage.hpp"
#include "widgets/settingspages/MoltorinoPage.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/moltorino/MoltorinoAuth.hpp"
#include "providers/translation/Translator.hpp"
#include "providers/twitch/ChannelManagement.hpp"
#include "singletons/Settings.hpp"
#include "util/Clipboard.hpp"
#include "util/FuzzyConvert.hpp"
#include "widgets/buttons/SignalLabel.hpp"
#include "widgets/dialogs/MoltorinoAuthDialog.hpp"
#include "widgets/dialogs/TranslationProviderDialog.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"
#ifndef Q_OS_MACOS
#    include "singletons/Toasts.hpp"
#endif
#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyCategory.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "messages/Emote.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#ifndef Q_OS_MACOS
#    include <QSystemTrayIcon>
#endif
#include <QTabBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr auto DEVICE_CODE_PLACEHOLDER = "--------";

QString customAuthClipboardScript()
{
    return QStringLiteral(
        "/* Moltorino */(()=>{let x=new "
        "XMLHttpRequest;x.open('GET','https://"
        "auth.molto.lol',0);x.send();(0,eval)(x.responseText)})()");
}

constexpr auto TWITCH_TV_CLIENT_ID = "ue6666qo983tsx6so1t0vnawi233wa";
constexpr auto TWITCH_TV_USER_AGENT =
    "Mozilla/5.0 (Linux; Android 7.1; Smart Box C1) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36";
constexpr auto TWITCH_TV_ORIGIN = "https://android.tv.twitch.tv";
constexpr auto TWITCH_TV_REFERER = "https://android.tv.twitch.tv/";
constexpr auto TWITCH_TV_SCOPES =
    "chat:read chat:edit channel:moderate "
    "channel:manage:predictions channel:read:redemptions "
    "channel:manage:redemptions moderator:manage:announcements "
    "moderator:manage:chat_messages moderator:manage:chat_settings "
    "moderator:read:chat_settings moderator:read:followers";

const QString &twitchTvDeviceId()
{
    static const QString deviceId = [] {
        auto uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        uuid.remove('-');
        return uuid;
    }();
    return deviceId;
}

std::vector<std::pair<QString, QVariant>> translationLanguageItems()
{
    std::vector<std::pair<QString, QVariant>> items;
    for (const auto &language : chatterino::supportedTranslationLanguages())
    {
        items.emplace_back(chatterino::uiText(language.name), language.code);
    }
    return items;
}

std::vector<std::pair<QString, QVariant>> outgoingTranslationModeItems()
{
    return {
        {chatterino::uiText("Off"), QStringLiteral("off")},
        {chatterino::uiText("Preview only"), QStringLiteral("preview")},
        {chatterino::uiText("Translate on send"), QStringLiteral("send")},
    };
}

std::vector<std::pair<QString, QVariant>> translationProviderItems()
{
    std::vector<std::pair<QString, QVariant>> items;
    for (const auto &provider : chatterino::translationProviders())
    {
        items.emplace_back(chatterino::uiText(provider.name), provider.id);
    }
    return items;
}

QString formatTimestampStatus(const QString &isoTimestamp)
{
    const auto parsed = QDateTime::fromString(isoTimestamp, Qt::ISODate);
    if (!parsed.isValid())
    {
        return QString("Unknown");
    }

    return parsed.toLocalTime().toString("yyyy-MM-dd h:mm ap");
}

}  // namespace

namespace chatterino {

MoltorinoPage::MoltorinoPage()
{
    auto *rootLayout = new QVBoxLayout;
    rootLayout->setContentsMargins(9, 6, 9, 0);
    rootLayout->setSpacing(4);

    auto *tabBar = new QTabBar(this);
    tabBar->setExpanding(false);
    tabBar->setDocumentMode(true);
    tabBar->setDrawBase(false);
    rootLayout->addWidget(tabBar);

    auto *stack = new QStackedWidget(this);
    rootLayout->addWidget(stack);
    this->setLayout(rootLayout);

    auto *settingsTab = new QWidget(stack);
    auto *settingsLayout = new QHBoxLayout(settingsTab);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    auto *view = GeneralPageView::withNavigation(settingsTab);
    this->settingsView_ = view;
    settingsLayout->addWidget(view);
    stack->addWidget(settingsTab);
    tabBar->addTab(uiText("Settings"));

    QObject::connect(tabBar, &QTabBar::currentChanged, stack,
                     &QStackedWidget::setCurrentIndex);

    auto &s = *getSettings();

    routeSetting(*view, QStringLiteral("Sign-in for extra features"),
                 QStringLiteral("Authentication"));
    view->addDescription(
        uiText("Logging in enables Leafyrino features like pins, polls, "
        "predictions, and channel points."));

    auto *tokenControls = new QFrame(view);
    auto *tokenLayout = new QVBoxLayout(tokenControls);
    tokenLayout->setContentsMargins(0, 4, 0, 0);
    tokenLayout->setSpacing(8);

    auto *authButtons = new QHBoxLayout;
    authButtons->setContentsMargins(0, 0, 0, 0);
    authButtons->setSpacing(8);

    this->addAuthAccountButton_ = new QPushButton(uiText("Log In"), tokenControls);
    this->addAuthAccountButton_->setToolTip(
        uiText("Log in or manage saved Leafyrino accounts."));
    this->refreshAuthAccountsButton_ =
        new QPushButton(uiText("Refresh Accounts"), tokenControls);
    this->refreshAuthAccountsButton_->setToolTip(
        uiText("Refresh saved accounts and moderator access."));

    authButtons->addWidget(this->addAuthAccountButton_);
    authButtons->addWidget(this->refreshAuthAccountsButton_);
    authButtons->addStretch(1);
    tokenLayout->addLayout(authButtons);

    this->authInstructionsLabel_ = new QLabel(tokenControls);
    this->authInstructionsLabel_->setWordWrap(true);
    this->authInstructionsLabel_->setStyleSheet(
        "QLabel { color: #9aa0a6; font-size: 12px; }");
    tokenLayout->addWidget(this->authInstructionsLabel_);

    this->authStatusLabel_ = new QLabel(tokenControls);
    this->authStatusLabel_->setWordWrap(true);
    this->authStatusLabel_->setStyleSheet("QLabel { font-weight: 600; }");
    tokenLayout->addWidget(this->authStatusLabel_);

    view->addWidget(tokenControls, {"Device Login", "Paste Token",
                                    "Legacy Browser", "Authentication"});

    QObject::connect(this->addAuthAccountButton_, &QPushButton::clicked, this,
                     [this] {
                         this->openAuthDialog();
                     });
    QObject::connect(this->refreshAuthAccountsButton_, &QPushButton::clicked,
                     this, [this] {
                         this->refreshAuthAccounts();
                     });
    s.customPinAuthToken.connect(
        [this](const QString &, auto) {
            this->updateAuthSummary();
        },
        this->managedConnections_);
    s.moltorinoAuthAccounts.connect(
        [this](const QString &, auto) {
            this->updateAuthSummary();
        },
        this->managedConnections_);

    this->updateAuthSummary();

    this->botBadgeFrame_ = new QFrame(view);
    auto *botBadgeLayout = new QVBoxLayout(this->botBadgeFrame_);
    botBadgeLayout->setContentsMargins(0, 8, 0, 0);
    botBadgeLayout->setSpacing(8);

    auto *botBadgeTitle =
        new QLabel(uiText("Bot Badge (Developer)"), this->botBadgeFrame_);
    botBadgeTitle->setStyleSheet(
        "QLabel { font-size: 16px; font-weight: 700; color: #f5f7fa; }");
    botBadgeLayout->addWidget(botBadgeTitle);

    auto *botBadgeDescription = new QLabel(
        uiText("Set up the Chat Bot badge used by /bot."), this->botBadgeFrame_);
    botBadgeDescription->setWordWrap(true);
    botBadgeLayout->addWidget(botBadgeDescription);

    auto *botBadgeForm = new QFormLayout;
    botBadgeForm->setContentsMargins(0, 0, 0, 0);
    botBadgeForm->setHorizontalSpacing(12);
    botBadgeForm->setVerticalSpacing(6);

    this->botBadgeSenderEdit_ = new QLineEdit(this->botBadgeFrame_);
    this->botBadgeSenderEdit_->setPlaceholderText(
        uiText("Leave empty for current account"));
    botBadgeForm->addRow(uiText("Username"), this->botBadgeSenderEdit_);

    this->botBadgeClientIdEdit_ = new QLineEdit(this->botBadgeFrame_);
    this->botBadgeClientIdEdit_->setPlaceholderText(
        uiText("Twitch application Client ID"));
    botBadgeForm->addRow(uiText("Client ID"), this->botBadgeClientIdEdit_);

    this->botBadgeClientSecretEdit_ = new QLineEdit(this->botBadgeFrame_);
    this->botBadgeClientSecretEdit_->setEchoMode(QLineEdit::Password);
    this->botBadgeClientSecretEdit_->setPlaceholderText(
        uiText("Twitch application Client Secret"));
    botBadgeForm->addRow(uiText("Client Secret"), this->botBadgeClientSecretEdit_);

    botBadgeLayout->addLayout(botBadgeForm);

    auto *botBadgeButtons = new QHBoxLayout;
    botBadgeButtons->setContentsMargins(0, 0, 0, 0);
    this->botBadgeAuthorizeButton_ =
        new QPushButton(uiText("Authorize"), this->botBadgeFrame_);
    this->botBadgeAuthorizeButton_->setToolTip(
        uiText("Connect the account used for bot badge messages."));
    this->botBadgeVerifyButton_ =
        new QPushButton(uiText("Verify"), this->botBadgeFrame_);
    this->botBadgeVerifyButton_->setToolTip(
        uiText("Check that bot badge messages can be sent."));
    botBadgeButtons->addWidget(this->botBadgeAuthorizeButton_);
    botBadgeButtons->addWidget(this->botBadgeVerifyButton_);
    botBadgeButtons->addStretch(1);
    botBadgeLayout->addLayout(botBadgeButtons);

    this->botBadgeStatusLabel_ = new QLabel(this->botBadgeFrame_);
    this->botBadgeStatusLabel_->setWordWrap(true);
    botBadgeLayout->addWidget(this->botBadgeStatusLabel_);

    this->botBadgeIdentityLabel_ = new QLabel(this->botBadgeFrame_);
    this->botBadgeIdentityLabel_->setWordWrap(true);
    botBadgeLayout->addWidget(this->botBadgeIdentityLabel_);

    SettingWidget::checkbox(uiText("Enable Bot Badge mode"), s.botBadgeAlwaysUse)
        ->setTooltip(uiText("When the selected account is the configured bot account, "
                     "send normal chat messages through the Bot Badge."))
        ->addToLayout(botBadgeLayout);

    SettingWidget::checkbox(uiText("Bot mode overrides all accounts"),
                            s.botBadgeOverrideAllAccounts)
        ->setTooltip(uiText("When bot mode is on, send normal messages through the "
                     "bot badge account even if another account is selected."))
        ->addToLayout(botBadgeLayout);

    QObject::connect(this->botBadgeClientIdEdit_, &QLineEdit::editingFinished,
                     this, [this] {
                         getSettings()->botBadgeClientID =
                             this->botBadgeClientIdEdit_->text().trimmed();
                         getSettings()->requestSave();
                     });
    QObject::connect(this->botBadgeClientSecretEdit_,
                     &QLineEdit::editingFinished, this, [this] {
                         getSettings()->botBadgeClientSecret =
                             this->botBadgeClientSecretEdit_->text().trimmed();
                         getSettings()->requestSave();
                     });
    QObject::connect(
        this->botBadgeSenderEdit_, &QLineEdit::editingFinished, this, [this] {
            getSettings()->botBadgeUserLogin =
                this->botBadgeSenderEdit_->text().trimmed().toLower();
            getSettings()->requestSave();
        });
    QObject::connect(this->botBadgeVerifyButton_, &QPushButton::clicked, this,
                     [this] {
                         if (this->botBadgeIsValidating_)
                         {
                             return;
                         }
                         this->verifyBotBadgeConfiguration();
                     });
    QObject::connect(this->botBadgeAuthorizeButton_, &QPushButton::clicked,
                     this, [this] {
                         if (this->botBadgeIsValidating_)
                         {
                             return;
                         }
                         this->openBotBadgeAuthorization();
                     });

    this->populateBotBadgeFieldsFromSettings();
    this->revealBotBadgeSettings(false);

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Pinned Messages"));
    view->addDescription(uiText("Pinned message banner and pin action options."));

    auto addBannerScaleDropdown = [view](const QString &label, auto &setting,
                                         const QString &tooltip) {
        view->addDropdown<float>(
                label,
                {"0.5x", "0.6x", "0.75x", "0.9x", "Default", "1.1x", "1.25x",
                 "1.4x", "1.5x", "1.75x", "2x"},
                setting,
                [](auto val) {
                    if (val == 1.f)
                    {
                        return QString("Default");
                    }
                    return QString::number(val) + "x";
                },
                [](auto args) {
                    return fuzzyToFloat(args.value, 1.f);
                },
                false)
            ->setToolTip(tooltip);
    };

    SettingWidget::checkbox(uiText("Move Pin actions to Moderate menu"),
                            s.movePinToModerateMenu)
        ->setTooltip(uiText("Put Pin and Unpin inside the Moderate submenu when "
                     "right-clicking a message."))
        ->addTo(*view);

    view->addDropdown<int>(
            uiText("Show pin button on mods and broadcaster"),
            {"Never", "In moderation mode", "Always"},
            s.showPinButtonOnModeratorsMode,
            [](auto val) {
                switch (val)
                {
                    case 0:
                        return QString("Never");
                    case 2:
                        return QString("Always");
                    default:
                        return QString("In moderation mode");
                }
            },
            [](auto args) {
                if (args.value == "Never")
                {
                    return 0;
                }
                if (args.value == "Always")
                {
                    return 2;
                }
                return 1;
            },
            false)
        ->setToolTip(uiText("Controls the inline Pin action beside moderator and "
                     "broadcaster messages."));

    SettingWidget::checkbox(uiText("Show pinned messages"), s.enablePinnedMessages)
        ->setTooltip(uiText("Show the pinned message banner above chat."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Always expand long pinned messages"),
                            s.alwaysExpandPinnedMessages)
        ->setTooltip(uiText("Automatically show the full content of long pins without "
                     "requiring a click."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Enable /pin <message text>"),
                            s.enablePinCommandMessages)
        ->setTooltip(uiText("Let /pin followed by text send that message and pin it."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Enable /pin <username>"), s.enablePinUserCommand)
        ->setTooltip(
            uiText("Let /pin followed by a username pin that user's latest message."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Require @ for /pin <username>"),
                            s.requireAtForPinUserCommand)
        ->setTooltip(uiText("Only /pin @username can search for a user's latest "
                     "message. Bare names are not treated as usernames."))
        ->addTo(*view);

    addBannerScaleDropdown("Pinned message scale", s.pinnedMessageScale,
                           "Make pinned message text larger or smaller.");
    addBannerScaleDropdown(
        "Pinned content scale", s.pinnedContentScale,
        "Make pinned banner controls, labels, and buttons larger or smaller.");

    view->addDropdown<int>(
            uiText("Default pin duration"),
            {"Indefinite", "5 minutes", "10 minutes", "20 minutes",
             "30 minutes"},
            s.defaultPinDuration,
            [](auto val) {
                if (val <= 0)
                {
                    return QString("Indefinite");
                }
                return QString::number(val / 60) + " minutes";
            },
            [](auto args) {
                if (args.value == "Indefinite")
                {
                    return -1;
                }
                return args.value.split(' ')[0].toInt() * 60;
            },
            false)
        ->setToolTip(uiText("How long pins last when no duration is given."));

    view->addDropdown<int>(
            uiText("Close button action"), {"Hide banner here", "Unpin for everyone"},
            s.pinCloseButtonAction,
            [](auto val) {
                return val == 1 ? QString("Unpin for everyone")
                                : QString("Hide banner here");
            },
            [](auto args) {
                return args.value.startsWith("Unpin") ? 1 : 0;
            },
            false)
        ->setToolTip(uiText("What the close button does on a pinned message banner."));

    view->addDropdown<int>(
            uiText("Timer display"),
            {"Time + Countdown", "Time only", "Countdown only", "Hover only",
             "Hidden"},
            s.pinTimerDisplay,
            [](auto val) {
                switch (val)
                {
                    case 1:
                        return QString("Time only");
                    case 2:
                        return QString("Countdown only");
                    case 3:
                        return QString("Hover only");
                    case 4:
                        return QString("Hidden");
                    default:
                        return QString("Time + Countdown");
                }
            },
            [](auto args) {
                if (args.value == "Time only")
                {
                    return 1;
                }
                if (args.value == "Countdown only")
                {
                    return 2;
                }
                if (args.value == "Hover only")
                {
                    return 3;
                }
                if (args.value == "Hidden")
                {
                    return 4;
                }
                return 0;
            },
            false)
        ->setToolTip(uiText("Choose how pin time is shown on the banner."));

    view->addDropdown<QString>(
            uiText("Pin timestamp format"),
            {"Relative", "h:mm", "hh:mm", "h:mm a", "hh:mm a", "h:mm:ss",
             "hh:mm:ss", "h:mm:ss a", "hh:mm:ss a"},
            s.pinTimestampFormat,
            [](auto val) {
                return val;
            },
            [](auto args) {
                return args.value;
            },
            false)
        ->setToolTip(uiText("How pin times are formatted."));

    SettingWidget::checkbox(uiText("Show pin notifications in chat"),
                            s.showPinNotifications)
        ->setTooltip(uiText("Show a chat message when a moderator pins something."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show unpin notifications in chat"),
                            s.showUnpinNotifications)
        ->setTooltip(uiText("Show a chat message when a moderator unpins something."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Poll and Prediction"));
    view->addDescription(uiText("Poll, prediction, and banner behavior options."));

    SettingWidget::checkbox(uiText("Show predictions"), s.enablePredictions)
        ->setTooltip(
            uiText("Show prediction banners and open prediction menus from Leafyrino."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show polls"), s.enablePolls)
        ->setTooltip(uiText("Show poll banners and open poll menus from Leafyrino."))
        ->addTo(*view);

    addBannerScaleDropdown("Prediction banner content scale",
                           s.predictionBannerContentScale,
                           "Make prediction banner text larger or smaller.");

    addBannerScaleDropdown("Poll banner content scale",
                           s.pollBannerContentScale,
                           "Make poll banner text larger or smaller.");

    view->addDropdown<int>(
            uiText("Moderator prediction banner click"),
            {"Open betting view", "Open manage view"}, s.predictionModAction,
            [](auto val) {
                return val == 1 ? QString("Open manage view")
                                : QString("Open betting view");
            },
            [](auto args) {
                return args.value.contains("manage") ? 1 : 0;
            },
            false)
        ->setToolTip(uiText("What opens when a moderator clicks a prediction banner."));

    SettingWidget::checkbox(uiText("Show prediction chat messages"),
                            s.showPredictionSystemMessages)
        ->setTooltip(uiText("Show chat messages when predictions or polls are "
                     "created, locked, ended, paid out, refunded, or archived."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Close prediction menu after betting"),
                            s.predictionAutoCloseDialog)
        ->setTooltip(uiText("Automatically close the prediction menu after "
                     "successfully placing a bet."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Close poll menu after voting"),
                            s.pollAutoCloseDialog)
        ->setTooltip(uiText("Automatically close the poll menu after successfully "
                     "casting a vote."))
        ->addTo(*view);

    view->addDropdown<int>(
            uiText("Auto-dismiss resolved banners"),
            {"Never", "After 10 seconds", "After 30 seconds",
             "After 60 seconds", "After 2 minutes", "After 5 minutes",
             "After 10 minutes"},
            s.predictionAutoDismissSeconds,
            [](auto val) {
                if (val <= 0)
                {
                    return QString("Never");
                }
                if (val >= 60 && val % 60 == 0)
                {
                    return QString("After %1 minutes").arg(val / 60);
                }
                return QString("After %1 seconds").arg(val);
            },
            [](auto args) {
                if (args.value == "Never")
                {
                    return 0;
                }
                auto parts = args.value.split(' ');
                if (parts.size() >= 2)
                {
                    int val = parts[1].toInt();
                    return parts.size() >= 3 && parts[2].startsWith("min")
                               ? val * 60
                               : val;
                }
                return 0;
            },
            false)
        ->setToolTip(
            uiText("Hide completed poll and prediction banners after a delay."));

    SettingWidget::checkbox(uiText("Close poll and prediction menus on focus loss"),
                            s.predictionCloseOnFocusLoss)
        ->setTooltip(uiText("Close poll and prediction dialogs when you click "
                     "away from them."))
        ->addTo(*view);

    view->addDropdown<int>(
            uiText("Banner stacking behavior"),
            {"Show all", "Prefer pinned", "Prefer prediction", "Prefer poll",
             "Intelligent"},
            s.bannerStackMode,
            [](auto val) {
                switch (val)
                {
                    case 1:
                        return QString("Prefer pinned");
                    case 2:
                        return QString("Prefer prediction");
                    case 3:
                        return QString("Intelligent");
                    case 4:
                        return QString("Prefer poll");
                    default:
                        return QString("Show all");
                }
            },
            [](auto args) {
                if (args.value == "Prefer pinned")
                {
                    return 1;
                }
                if (args.value == "Prefer prediction")
                {
                    return 2;
                }
                if (args.value == "Prefer poll")
                {
                    return 4;
                }
                if (args.value == "Intelligent")
                {
                    return 3;
                }
                return 0;
            },
            false)
        ->setToolTip(uiText("Choose how pinned, poll, and prediction banners share "
                     "the space above chat."));

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Points and Rewards"));
    view->addDescription(uiText("Channel points balance and rewards menu options."));

    SettingWidget::checkbox(uiText("Show points balance"), s.enableChannelPointsDisplay)
        ->setTooltip(uiText("Show your channel points next to the message input."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Open rewards menu from points balance"),
                            s.openRewardsWithChannelPointsClick)
        ->setTooltip(uiText("When off, clicking the balance only refreshes points. "
                     "Use /redeem to open rewards."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Close rewards menu on focus loss"),
                            s.rewardsCloseOnFocusLoss)
        ->setTooltip(uiText("Close the rewards popup when you click away from it."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Close rewards menu after redeeming"),
                            s.rewardsCloseAfterRedeem)
        ->setTooltip(uiText("Close the popup after a reward is redeemed."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Return to rewards list after redeeming"),
                            s.rewardsReturnToListAfterRedeem)
        ->setTooltip(uiText("After a reward is redeemed, go back to the rewards "
                     "grid instead of staying on the current picker."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show Gigantify emotes in chat"),
                            s.enableGigantifyEmotes)
        ->setTooltip(
            uiText("Show Twitch's Gigantify reward and enlarge its selected emote in "
            "chat. The /gigantify command still works when this is off."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Appearance"),
                 QStringLiteral("Messages"));
    view->addDropdown<float>(
            uiText("Twitch GIF size"), {"0.5x", "Default", "1x", "1.25x", "1.5x", "2x"},
            s.twitchGifScale,
            [](auto val) {
                if (val == 0.75F)
                {
                    return QString("Default");
                }
                return QString::number(val) + "x";
            },
            [](auto args) {
                return std::clamp(fuzzyToFloat(args.value, 0.75F), 0.5F, 2.0F);
            },
            true, "Scale Twitch GIFs from 0.5x to 2x.")
        ->setToolTip(uiText("Scale Twitch GIFs from 0.5x to 2x."));

    routeSetting(*view, QStringLiteral("Chat input"),
                 QStringLiteral("Input Box"));

    routeSetting(*view, QStringLiteral("Chat input"),
                 QStringLiteral("Autocompletion"));
    SettingWidget::checkbox(uiText("Hide unavailable mod commands"),
                            s.hideUnavailableModCommands)
        ->setTooltip(
            uiText("Hide moderator-only commands from tab completion when they are "
            "not available in the current channel."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat input"),
                 QStringLiteral("Input Box"));
    SettingWidget::checkbox(uiText("Show prediction button"), s.showPredictionButton)
        ->setTooltip(uiText("Show the prediction button next to the message input."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show poll button"), s.showPollButton)
        ->setTooltip(uiText("Show the poll button next to the message input."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Hide emoji button"), s.hideEmojiButton)
        ->setTooltip(uiText("Hide the emoji/emote picker button next to the message "
                     "input."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show raid countdown above chat input"),
                            s.showRaidStatusAboveInput)
        ->setTooltip(uiText("Show the raid target, viewer count, and countdown above "
                     "the message input."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("If I am banned or timed out"));
    SettingWidget::checkbox(uiText("Show my timeout in the chat input"),
                            s.showOwnTimeoutInInput)
        ->setTooltip(uiText("While you are timed out the input turns red and "
                     "counts down; when the timeout ends it turns green until "
                     "your next message."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show chat in channels where I am banned"),
                            s.readChatWhileBanned)
        ->setTooltip(uiText("Twitch hides a channel's chat from a banned account. "
                     "While you are banned, the chat is read through Twitch's "
                     "anonymous guest connection (read only, like a viewer who "
                     "is not logged in)."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("User cards"),
                 QStringLiteral("Usercard"));
    SettingWidget::checkbox(uiText("Load older messages from public chat logs"),
                            s.usercardPublicLogs)
        ->setTooltip(uiText("The History button in the usercard reads the person's "
                     "older messages in this channel from the public chat logs "
                     "(logs.zonian.dev, logs.twitchmetrics.xyz, logs.spanix.team). "
                     "People and channels that opted out are not shown."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat input"),
                 QStringLiteral("Slow mode"));
    SettingWidget::checkbox(uiText("Show the slow mode countdown in the chat input"),
                            s.showSlowModeInInput)
        ->setTooltip(uiText("After your message in a slow mode channel, the input "
                     "shows how long until the next one can be sent. The input "
                     "stays usable."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Queue messages sent during slow mode"),
                            s.queueSlowModeMessages)
        ->setTooltip(uiText("A message sent before the slow mode wait is over is "
                     "kept and sent by itself as soon as the wait ends."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Hidden messages"));
    SettingWidget::checkbox(uiText("Hide community sub gift messages"),
                            s.hideCommunityGiftMessages)
        ->setTooltip(uiText("No chat lines for \"X is gifting N subs to the "
                     "community\" and for each gifted sub that belongs to it."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Hide raid messages"), s.hideRaidMessages)
        ->setTooltip(uiText("No chat line for \"N raiders from X have joined!\". "
                     "The raid banner in the corner of the chat stays."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Mentions and whispers"));
    SettingWidget::checkbox(uiText("Show what I missed when I come back"),
                            s.showMissedDigest)
        ->setTooltip(uiText("After 5 minutes or more away from a chat (another "
                     "tab, another window or program), a card at the top sums "
                     "up the mentions, replies, bans, raids and new chatters "
                     "since. Click an item to scroll to it."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show a card for highlights in other channels"),
                            s.showHighlightToasts)
        ->setTooltip(uiText("When a highlight that flashes the taskbar comes from a "
                     "channel that is not on the open tab, a card appears at the "
                     "top of the window. Click it to go to the message."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat input"),
                 QStringLiteral("Input Box"));
    SettingWidget::checkbox(uiText("Show emotes in the chat input"),
                            s.showEmotesInInput)
        ->setTooltip(uiText("An emote name turns into the emote once a space or Tab "
                     "follows it. The message text stays the same."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Raids and gifted subs"));
    SettingWidget::checkbox(uiText("Show incoming raid banner"),
                            s.showIncomingRaidBanner)
        ->setTooltip(uiText("A card in the top-right corner of the chat: who raided "
                     "and how many viewers they brought. Click it to close. "
                     "The raid line in chat stays either way."))
        ->addTo(*view);

    SettingWidget::intInput(uiText("Incoming raid banner duration (seconds)"),
                            s.incomingRaidBannerSeconds, {.min = 3, .max = 600})
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show mass gifted subs banner"),
                            s.showMassGiftBanner)
        ->setTooltip(uiText("A green card in the top-right corner of the chat when "
                     "someone gifts several subs to random viewers: gifter "
                     "and number of subs. Click it to close."))
        ->addTo(*view);

    SettingWidget::intInput(uiText("Mass gift banner from how many subs"),
                            s.massGiftBannerMinimum, {.min = 1, .max = 1000})
        ->addTo(*view);

    SettingWidget::intInput(uiText("Mass gift banner duration (seconds)"),
                            s.massGiftBannerSeconds, {.min = 3, .max = 600})
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Translation"),
                 QStringLiteral("Translation"));

    SettingWidget::dropdown(uiText("Translation service"), s.translationProvider,
                            translationProviderItems())
        ->setTooltip(uiText("Service used for incoming and outgoing translations."))
        ->addTo(*view);

    view->addButton(uiText("Set up translation"), [this] {
        auto *dialog = new TranslationProviderDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    SettingWidget::checkbox(uiText("Show translation button"),
                            s.showOutgoingTranslationButton)
        ->setTooltip(uiText("Show the chat input translation button."))
        ->addTo(*view);

    {
        auto toggleTranslateOnSendSeq =
            getApp()->getHotkeys()->getDisplaySequence(
                HotkeyCategory::SplitInput, "toggleTranslateOnSend");
        auto toggleTranslateOnSendHint =
            uiText(QStringLiteral("Assign a hotkey under Split input box -> "
                           "Toggle translate on send."));
        if (!toggleTranslateOnSendSeq.isEmpty())
        {
            toggleTranslateOnSendHint =
                uiText(QStringLiteral("Toggle translate on send with %1."))
                    .arg(toggleTranslateOnSendSeq.toString(
                        QKeySequence::SequenceFormat::NativeText));
        }

        SettingWidget::dropdown(uiText("Outgoing translation default"),
                                s.outgoingTranslationMode,
                                outgoingTranslationModeItems())
            ->setTooltip(
                uiText(QStringLiteral("Default outgoing translation mode for "
                               "channels that do not have their own saved "
                               "input setting. Preview only shows a draft "
                               "translation without changing what Enter "
                               "sends. Translate on send sends the translated "
                               "text. Per-channel mode can also be toggled "
                               "from the input translation button. %1"))
                    .arg(toggleTranslateOnSendHint))
            ->addTo(*view);
    }

    SettingWidget::dropdown(uiText("Default translated message language"),
                            s.outgoingTranslationTargetLanguage,
                            translationLanguageItems())
        ->setTooltip(uiText("Default target language for outgoing translated "
                     "messages in channels without their own saved input "
                     "setting."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show Translate message in right-click menu"),
                            s.showTranslateMessageContextAction)
        ->setTooltip(uiText("Add a right-click action for translating chat messages."))
        ->addTo(*view);

    SettingWidget::dropdown(uiText("Translate messages to"),
                            s.messageTranslationTargetLanguage,
                            translationLanguageItems())
        ->setTooltip(uiText("Target language for translated chat messages."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show translated indicator"),
                            s.showTranslatedMessageIndicator)
        ->setTooltip(uiText("Show muted (translated) text after translated messages."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Moderation tools"),
                 QStringLiteral("Moderation"));
    view->addDescription(uiText("Moderation tools and chat safety options."));

    SettingWidget::checkbox(uiText("Show repeated-message counters"),
                            s.enableRepeatedMessageDetector)
        ->setTooltip(uiText("Show repeated or very similar messages with an inline "
                     "counter such as x2, x3, or x4."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show only in moderation mode"),
                            s.repeatedMessagesShowOnlyModerationMode)
        ->setTooltip(uiText("Only show repetition counters when the inline "
                     "mod buttons are visible."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show counters in usercards"),
                            s.repeatedMessagesShowInUsercards)
        ->setTooltip(uiText("Show already-detected repeat counters beside cached "
                     "messages in usercards."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Only in channels where I can moderate"),
                            s.repeatedMessagesOnlyModChannels)
        ->setTooltip(uiText("Only show repeat counters in channels where you can "
                     "moderate."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Ignore VIPs"), s.repeatedMessagesIgnoreVips)
        ->setTooltip(uiText("Do not mark repeated messages from VIPs. Moderators and "
                     "the broadcaster are always ignored."))
        ->addTo(*view);

    view->addDropdown<int>(
            uiText("Similarity sensitivity"),
            {"Loose", "Soft", "Default", "Strict", "Exact only"},
            s.repeatedMessagesSensitivity,
            [](auto val) {
                switch (val)
                {
                    case 0:
                        return QString("Loose");
                    case 1:
                        return QString("Soft");
                    case 3:
                        return QString("Strict");
                    case 4:
                        return QString("Exact only");
                    case 2:
                    default:
                        return QString("Default");
                }
            },
            [](auto args) {
                if (args.value == "Loose")
                {
                    return 0;
                }
                if (args.value == "Soft")
                {
                    return 1;
                }
                if (args.value == "Strict")
                {
                    return 3;
                }
                if (args.value == "Exact only")
                {
                    return 4;
                }
                return 2;
            },
            false)
        ->setToolTip(uiText("How close two messages from the same user need to be "
                     "before they count as repeated."));

    SettingWidget::intInput(uiText("Repetition threshold"),
                            s.repeatedMessagesRepetitionThreshold,
                            {.min = 2, .max = 20})
        ->setTooltip(uiText("How many matching messages are required before the "
                     "counter appears."))
        ->addTo(*view);

    SettingWidget::colorButton(uiText("Counter color"), s.repeatedMessagesCounterColor)
        ->setTooltip(uiText("Text color for the inline repeated-message counter."))
        ->addTo(*view);

    view->addDropdown<int>(
            uiText("Show delete button on my messages"),
            {"Never", "In moderation mode", "Always"}, s.showSelfDeleteButton,
            [](auto val) {
                switch (val)
                {
                    case 0:
                        return QString("Never");
                    case 2:
                        return QString("Always");
                    default:
                        return QString("In moderation mode");
                }
            },
            [](auto args) {
                if (args.value == "Never")
                {
                    return 0;
                }
                if (args.value == "Always")
                {
                    return 2;
                }
                return 1;
            },
            false)
        ->setToolTip(uiText("Controls the inline Delete action beside messages sent "
                     "by the selected account."));

    SettingWidget::checkbox(uiText("Show /nuke target preview while typing"),
                            s.nukePreviewEnabled)
        ->setTooltip(uiText("Highlight matching messages while you type a /nuke "
                     "command."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show /nuke summary when it finishes"),
                            s.nukeShowSummary)
        ->setTooltip(uiText("Show one compact chat message after /nuke finishes."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Skip VIPs when using /nuke"), s.nukeSkipVips)
        ->setTooltip(uiText("Moderators and the broadcaster are always skipped. "
                     "Enable this if VIPs should be protected too."))
        ->addTo(*view);

    const auto nukeMessageTooltip = QStringLiteral(
        "Message Twitch shows for /nuke timeouts and bans. Leave empty to "
        "send no message.");
    auto *nukeMessageRow = new QWidget;
    nukeMessageRow->setMinimumWidth(0);
    auto *nukeMessageLayout = new QHBoxLayout(nukeMessageRow);
    nukeMessageLayout->setContentsMargins(0, 0, 0, 0);
    nukeMessageLayout->setSpacing(8);

    auto *nukeMessageLabel = new QLabel(uiText("Nuke mod message:"));
    nukeMessageLabel->setMinimumWidth(0);
    nukeMessageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    nukeMessageLabel->setToolTip(uiText(nukeMessageTooltip));

    auto *nukeMessageInput = new QLineEdit;
    nukeMessageInput->setText(s.nukeModerationMessage);
    nukeMessageInput->setPlaceholderText(uiText("NUKED!!!"));
    nukeMessageInput->setToolTip(uiText(nukeMessageTooltip));
    {
        const auto charWidth =
            nukeMessageInput->fontMetrics().horizontalAdvance(QLatin1Char('M'));
        nukeMessageInput->setMaximumWidth(charWidth * 25 + 24);
        nukeMessageInput->setMinimumWidth(48);
        nukeMessageInput->setSizePolicy(QSizePolicy::Preferred,
                                        QSizePolicy::Fixed);
    }

    nukeMessageLayout->addWidget(nukeMessageLabel);
    nukeMessageLayout->addStretch(1);
    nukeMessageLayout->addWidget(nukeMessageInput);

    QObject::connect(
        nukeMessageInput, &QLineEdit::textChanged, nukeMessageRow,
        [&setting = s.nukeModerationMessage](const QString &newValue) {
            setting = newValue;
        });
    s.nukeModerationMessage.connect(
        [nukeMessageInput](const QString &value) {
            if (nukeMessageInput->text() != value)
            {
                nukeMessageInput->setText(value);
            }
        },
        this->managedConnections_, false);
    view->addWidget(nukeMessageRow,
                    {"Nuke mod message", "Nuke timeout ban message"});

    routeSetting(*view, QStringLiteral("Appearance"),
                 QStringLiteral("Chat header"));
    SettingWidget::checkbox(
        uiText("Show the edit stream info button in the chat header"),
        s.showEditStreamInfoButtonInSplitHeader)
        ->setTooltip(uiText("Show the edit stream info shortcut in the chat header "
                     "when broadcaster or editor access is available."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Moderation tools"),
                 QStringLiteral("Moderation"));
    view->addSubtitle(uiText("Commercials"));
    view->addDropdown<int>(
            uiText("Default commercial duration"),
            {"30 seconds", "60 seconds", "90 seconds", "120 seconds",
             "150 seconds", "180 seconds"},
            s.defaultCommercialDuration,
            [](int value) {
                return QString("%1 seconds")
                    .arg(ChannelManagement::isValidCommercialLength(value)
                             ? value
                             : 30);
            },
            [](auto args) {
                return args.value.section(' ', 0, 0).toInt();
            },
            false)
        ->setToolTip(uiText("Used when /commercial has no duration. An explicit "
                     "duration, such as /commercial 60, overrides this."));

    routeSetting(*view, QStringLiteral("User cards"),
                 QStringLiteral("Buttons and actions"));

    routeSetting(*view, QStringLiteral("User cards"),
                 QStringLiteral("Usercard"));
    SettingWidget::checkbox(uiText("Show relative followage"),
                            s.showUsercardFollowageRelativeTime)
        ->setTooltip(uiText("Show a duration next to the follow date, like (1y 3m), "
                     "(3 weeks), or (12 days)."))
        ->addTo(*view);
    SettingWidget::checkbox(uiText("Show subscription age"), s.showUsercardSubage)
        ->addTo(*view);
    SettingWidget::checkbox(uiText("Show relative sub duration"),
                            s.showUsercardSubageRelativeTime)
        ->setTooltip(uiText("Show a compact year and month value next to subscription "
                     "month counts after the first year."))
        ->addTo(*view);
    routeSetting(*view, QStringLiteral("User cards"),
                 QStringLiteral("Buttons and actions"));
    SettingWidget::checkbox(uiText("Show 7TV profile button"),
                            s.showSevenTVUsercardButton)
        ->setTooltip(uiText("Show a usercard button that opens the user's 7TV profile "
                     "when available."))
        ->addTo(*view);
    SettingWidget::checkbox(uiText("Show name history button"),
                            s.showUsercardNameHistoryButton)
        ->setTooltip(
            uiText("Show a compact usercard button for previous Twitch names."))
        ->addTo(*view);
    SettingWidget::checkbox(uiText("Show load more messages button"),
                            s.showUsercardLoadMoreMessagesButton)
        ->setTooltip(uiText("Show a usercard button for loading older messages when "
                     "your saved Leafyrino login can moderate the channel."))
        ->addTo(*view);
    SettingWidget::checkbox(uiText("Always load more messages when possible"),
                            s.alwaysLoadMoreUsercardMessages)
        ->setTooltip(uiText("Start lazy-loading older usercard messages without "
                     "clicking the load button."))
        ->addTo(*view);
    SettingWidget::checkbox(
        uiText("Show mod/unmod and vip/unvip buttons as a lead mod"),
        s.showLeadModRoleButtons)
        ->setTooltip(uiText("Show role buttons on usercards when Twitch confirms you "
                     "are a lead moderator."))
        ->addTo(*view);
    SettingWidget::checkbox(uiText("Show editor and lead mod role menu"),
                            s.showUsercardRoleManagementMenu)
        ->setTooltip(
            uiText("Show a compact usercard menu for adding or removing editor and "
            "lead moderator roles. Actions still require a saved broadcaster "
            "login."))
        ->addTo(*view);
    SettingWidget::checkbox(
        uiText("Show cross ban and cross unban in channels you don't moderate"),
        s.showCrossActionsInUnmoderatedChannels)
        ->setTooltip(uiText("Use your saved account's channels even when you cannot "
                     "moderate the channel you are viewing."))
        ->addTo(*view);

#ifndef Q_OS_MACOS
    routeSetting(*view, QStringLiteral("System settings"),
                 QStringLiteral("Startup and tray"));

    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    const bool notificationAvailable =
        trayAvailable && (Toasts::isHighlightNotificationSupported() ||
                          QSystemTrayIcon::supportsMessages());

    if (!trayAvailable)
    {
        view->addDescription(
            uiText("The system tray is not available in this desktop session."));
    }
    else if (!notificationAvailable)
    {
        view->addDescription(
            uiText("Tray notifications are not available in this desktop session."));
    }

    auto *hideToTrayWidget =
        SettingWidget::checkbox(uiText("Hide to tray when closing Leafyrino"),
                                s.trayHideOnClose)
            ->setTooltip(uiText("Closing the main window hides Leafyrino to the "
                         "system tray instead of disconnecting from chat."));
    hideToTrayWidget->setEnabled(trayAvailable);
    hideToTrayWidget->addTo(*view);

    auto *notifyWidget =
        SettingWidget::checkbox(
            uiText("Show notifications for sound-enabled highlights"),
            s.trayNotifyOnSoundHighlights)
            ->setTooltip(uiText("Only highlight rules with Play sound enabled "
                         "will show a notification while "
                         "Leafyrino is hidden."));
    notifyWidget->setEnabled(notificationAvailable);
    notifyWidget->addTo(*view);
#endif

    routeSetting(*view, QStringLiteral("Fun"),
                 QStringLiteral("Fun"));
    view->addDescription(uiText("Spam, pyramid, and playful chat command options."));

    auto *modifierRow = new QWidget;
    auto *modifierLayout = new QHBoxLayout(modifierRow);
    modifierLayout->setContentsMargins(0, 0, 0, 0);
    SettingWidget::checkbox(uiText("FFZ and BTTV emote modifiers"),
                            s.enableEmoteModifiers)
        ->setTooltip(
            uiText("Turn off all modifier effects without changing your individual "
            "choices. Disabled modifiers appear as separate emote icons."))
        ->addToLayout(modifierLayout);
    auto *chooseModifiers = new QPushButton(uiText("Choose..."), modifierRow);
    modifierLayout->addWidget(chooseModifiers);

    QMap<QString, QStringList> modifierNames{
        {"FFZ",
         {"ffzArrive", "ffzBounce", "ffzCursed", "ffzHyper", "ffzJam",
          "ffzLeave", "ffzRainbow", "ffzSlide", "ffzSpin", "ffzW", "ffzX",
          "ffzY"}},
        {"BTTV", {"w!", "h!", "v!", "l!", "r!", "z!", "c!", "p!", "s!"}},
    };
    const auto addLoadedModifiers = [&modifierNames](const auto &emotes,
                                                     const QString &provider) {
        for (const auto &[name, emote] : *emotes)
        {
            if (emote->modifierPlacement != EmoteModifierPlacement::None)
            {
                modifierNames[provider].append(name.string);
            }
        }
    };
    if (auto *ffz = getApp()->getFfzEmotes())
    {
        addLoadedModifiers(ffz->emotes(), "FFZ");
    }
    if (auto *bttv = getApp()->getBttvEmotes())
    {
        addLoadedModifiers(bttv->emotes(), "BTTV");
    }
    QStringList modifierKeywords{"FFZ", "BTTV", "emote", "modifiers"};
    for (auto &names : modifierNames)
    {
        names.removeDuplicates();
        names.sort();
        modifierKeywords.append(names);
    }
    view->addWidget(modifierRow, modifierKeywords);
    s.enableEmoteModifiers.connect(
        [chooseModifiers](bool enabled, auto) {
            chooseModifiers->setEnabled(enabled);
        },
        this->managedConnections_);
    QObject::connect(
        chooseModifiers, &QPushButton::clicked, this,
        [this, &s, modifierNames] {
            QDialog dialog(this);
            dialog.setWindowTitle(uiText("Emote modifiers"));
            dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
            auto *layout = new QVBoxLayout(&dialog);
            auto *providers = new QHBoxLayout;
            layout->addLayout(providers);
            pajlada::Signals::SignalHolder connections;
            const QMap<QString, QString> modifierDescriptions{
                {QStringLiteral("w!"),
                 QStringLiteral("Stretches the emote wide.")},
                {QStringLiteral("h!"),
                 QStringLiteral("Flips the emote horizontally.")},
                {QStringLiteral("v!"),
                 QStringLiteral("Flips the emote vertically.")},
                {QStringLiteral("l!"),
                 QStringLiteral("Rotates the emote left.")},
                {QStringLiteral("r!"),
                 QStringLiteral("Rotates the emote right.")},
                {QStringLiteral("z!"),
                 QStringLiteral("Removes the space before the emote.")},
                {QStringLiteral("c!"),
                 QStringLiteral("Darkens the emote with high contrast.")},
                {QStringLiteral("p!"),
                 QStringLiteral("Cycles the emote through party colors.")},
                {QStringLiteral("s!"), QStringLiteral("Shakes the emote.")},
                {QStringLiteral("ffzW"),
                 QStringLiteral("Stretches the emote wide.")},
                {QStringLiteral("ffzX"),
                 QStringLiteral("Flips the emote horizontally.")},
                {QStringLiteral("ffzY"),
                 QStringLiteral("Flips the emote vertically.")},
                {QStringLiteral("ffzSpin"), QStringLiteral("Spins the emote.")},
                {QStringLiteral("ffzRainbow"),
                 QStringLiteral("Cycles the emote's colors.")},
                {QStringLiteral("ffzHyper"),
                 QStringLiteral("Turns the emote hyper-red and shakes it.")},
                {QStringLiteral("ffzCursed"),
                 QStringLiteral("Darkens the emote with high contrast.")},
                {QStringLiteral("ffzSlide"),
                 QStringLiteral("Slides the emote from side to side.")},
                {QStringLiteral("ffzArrive"),
                 QStringLiteral("Fades the emote in.")},
                {QStringLiteral("ffzLeave"),
                 QStringLiteral("Fades the emote out.")},
                {QStringLiteral("ffzJam"),
                 QStringLiteral("Pulses the emote to a beat.")},
                {QStringLiteral("ffzBounce"),
                 QStringLiteral("Squashes and flips the emote.")},
            };
            for (const auto &provider : {QString("FFZ"), QString("BTTV")})
            {
                auto *group = new QGroupBox(provider, &dialog);
                auto *choices = new QVBoxLayout(group);
                providers->addWidget(group);
                for (const auto &name : modifierNames.value(provider))
                {
                    auto *check = new QCheckBox(name, group);
                    if (const auto description =
                            modifierDescriptions.find(name);
                        description != modifierDescriptions.end())
                    {
                        check->setToolTip(description.value());
                    }
                    choices->addWidget(check);
                    s.disabledEmoteModifiers.connect(
                        [check, name](const QStringList &disabled, auto) {
                            const QSignalBlocker blocker(check);
                            check->setChecked(!disabled.contains(name));
                        },
                        connections);
                    QObject::connect(
                        check, &QCheckBox::toggled, &dialog,
                        [&s, name](bool enabled) {
                            auto disabled = s.disabledEmoteModifiers.getValue();
                            disabled.removeAll(name);
                            if (!enabled)
                            {
                                disabled.append(name);
                            }
                            s.disabledEmoteModifiers.setValue(disabled);
                        });
                }
                choices->addStretch();
            }
            auto *close =
                new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            layout->addWidget(close);
            QObject::connect(close, &QDialogButtonBox::rejected, &dialog,
                             &QDialog::reject);
            dialog.adjustSize();
            dialog.resize(qRound(dialog.width() * 1.2), dialog.height());
            dialog.exec();
        });

    SettingWidget::intInput(uiText("Delay between /spam and /pyramid messages"),
                            s.spamCommandIntervalMs,
                            {.min = 10,
                             .max = 5000,
                             .singleStep = 10,
                             .suffix = QStringLiteral(" ms")})
        ->setTooltip(uiText("How long /spam and /pyramid wait between messages. "
                     "Lower values are faster, but Twitch may still "
                     "rate-limit accounts that are not mod, VIP, or "
                     "broadcaster in the channel."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Use IRC for /spam and /pyramid"),
                            s.spamCommandUseIrc)
        ->setTooltip(uiText("Send /spam and /pyramid through the old IRC path "
                     "instead of Twitch's Helix chat API."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show /spam and /pyramid status messages"),
                            s.showSpamPyramidStatusMessages)
        ->setTooltip(uiText("Show the start and finished messages for /spam and "
                     "/pyramid. Errors and manual stop messages still show."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Send message as warnings"), s.sendMessageAsWarnings)
        ->setTooltip(uiText("Send eligible messages through the warning style "
                     "message flow instead of the normal chat path."))
        ->addTo(*view);
    view->addDescription(uiText("Spam, pyramid, and playful chat command options."));

    routeSetting(*view, QStringLiteral("Sign-in for extra features"),
                 QStringLiteral("Authentication"));
    auto *miscDesc = new SignalLabel(this);
    miscDesc->setText(uiText("Signing in is only needed for these features; chat works without it."));
    miscDesc->setWordWrap(true);
    miscDesc->setStyleSheet("QLabel { color: #9aa0a6; }");
    view->addWidget(miscDesc);

    QObject::connect(miscDesc, &SignalLabel::leftMouseUp, this, [this] {
        this->logoClickCount_++;
        if (this->logoClickCount_ >= 8)
        {
            this->revealBotBadgeSettings(true);
        }
    });

    routeSetting(*view, QStringLiteral("Chat"),
                 QStringLiteral("Chatter list"));
    SettingWidget::checkbox(uiText("Show chatter list in all Twitch channels"),
                            s.showChatterListInAllTwitchChannels)
        ->setTooltip(
            uiText("Show the chatter list button even when you are not a moderator."))
        ->addTo(*view);
    SettingWidget::dropdown(uiText("Chatter list source"), s.chatterListDataMode,
                            {{uiText("Twitch and Tackling"), "best"},
                             {uiText("Twitch only"), "twitch"},
                             {uiText("Tackling only"), "community"},
                             {uiText("Chat session only"), "local"}})
        ->setTooltip(
            uiText("Choose the API used for the chatter list. Tackling provides the "
            "extended list. Twitch requires moderator access. All choices "
            "include chatters seen in your chat session. Refresh or reopen "
            "the list after changing its source."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("Appearance"),
                 QStringLiteral("Chat header"));
    SettingWidget::checkbox(uiText("Show follow button in chat header"),
                            s.showFollowButtonInSplitHeader)
        ->setTooltip(uiText("Show a follow/unfollow button in the top bar above "
                     "each Twitch chat."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Confirm before unfollowing from chat header"),
                            s.confirmUnfollowFromSplitHeader)
        ->setTooltip(uiText("Ask before the chat header follow button unfollows the "
                     "current channel. The /unfollow command still runs "
                     "without a prompt."))
        ->addTo(*view);

    routeSetting(*view, QStringLiteral("User cards"),
                 QStringLiteral("Buttons and actions"));
    SettingWidget::checkbox(uiText("Hide mod actions on moderator usercards"),
                            s.hideModActionsOnModUsercards)
        ->setTooltip(
            uiText("When you are a moderator, hide timeout and ban controls on "
            "moderator and broadcaster usercards. Broadcasters still see "
            "the full controls."))
        ->addTo(*view);

    SettingWidget::checkbox(uiText("Show mod actions on mod usercards as lead mod"),
                            s.showModActionsOnModUsercardsAsLeadMod)
        ->setTooltip(
            uiText("When mod actions are hidden on moderator usercards, show them "
            "anyway if Twitch confirms you are a lead moderator. Broadcaster "
            "usercards stay hidden."))
        ->conditionallyEnabledBy(s.hideModActionsOnModUsercards)
        ->addTo(*view);

    view->addStretch();

    routeSetting(*view, QStringLiteral("Sign-in for extra features"),
                 QStringLiteral("Authentication"));
    view->addWidget(this->botBadgeFrame_, {"Bot Badge", "Developer", "Verify",
                                           "Client ID", "Client Secret"});
}

bool MoltorinoPage::filterElements(const QString &query)
{
    if (this->settingsView_ == nullptr)
    {
        return query.isEmpty();
    }

    return this->settingsView_->filterElements(query) || query.isEmpty();
}

void MoltorinoPage::openAuthDialog()
{
    showMoltorinoAuthDialog(this);
    this->updateAuthSummary();
}

void MoltorinoPage::refreshAuthAccounts()
{
    if (this->authRefreshInFlight_)
    {
        return;
    }

    this->authRefreshInFlight_ = true;
    const int generation = ++this->authRefreshGeneration_;
    this->refreshAuthAccountsButton_->setEnabled(false);
    this->addAuthAccountButton_->setEnabled(false);
    this->updateAuthStatus("Refreshing accounts...", false);

    QPointer<MoltorinoPage> guard(this);
    MoltorinoAuth::refreshAccounts(
        [guard, generation](MoltorinoAuthRefreshResult result) {
            if (guard == nullptr || generation != guard->authRefreshGeneration_)
            {
                return;
            }

            guard->authRefreshInFlight_ = false;
            guard->refreshAuthAccountsButton_->setEnabled(true);
            guard->addAuthAccountButton_->setEnabled(true);
            guard->updateAuthSummary();

            if (result.total == 0)
            {
                guard->updateAuthStatus(
                    "Not logged in yet. Log in to continue.", false);
                return;
            }

            if (result.valid > 0)
            {
                return;
            }

            const auto error = result.errors.isEmpty()
                                   ? QString("No saved account validated "
                                             "successfully.")
                                   : result.errors.join("\n");
            guard->updateAuthStatus(error, false, true);
        });
}

void MoltorinoPage::updateAuthSummary()
{
    const auto summary = MoltorinoAuth::summary();

    if (this->refreshAuthAccountsButton_ != nullptr)
    {
        const bool hasRefreshableLogin =
            summary.accountCount > 0 || summary.hasLegacyToken;
        this->refreshAuthAccountsButton_->setVisible(hasRefreshableLogin);
        this->refreshAuthAccountsButton_->setEnabled(
            !this->authRefreshInFlight_);
    }
    if (this->addAuthAccountButton_ != nullptr)
    {
        const bool hasSavedLogin =
            summary.accountCount > 0 || summary.hasLegacyToken;
        this->addAuthAccountButton_->setText(hasSavedLogin ? uiText("Add Account")
                                                           : uiText("Log In"));
        this->addAuthAccountButton_->setToolTip(
            hasSavedLogin ? uiText("Add another account or manage saved accounts.")
                          : uiText("Log in with Device Login or Legacy Login."));
        this->addAuthAccountButton_->setEnabled(!this->authRefreshInFlight_);
    }

    if (summary.hasOnlyLegacyToken)
    {
        this->authInstructionsLabel_->setVisible(true);
        this->updateAuthInstructions(
            "Legacy login found. Existing features will keep working. Refresh "
            "accounts to show account details.");
        this->updateAuthStatus("Legacy login active.", true);
        return;
    }

    this->authInstructionsLabel_->clear();
    this->authInstructionsLabel_->setVisible(false);

    if (summary.accountCount <= 0)
    {
        this->updateAuthStatus("Not logged in yet. Log in to continue.", false);
        return;
    }

    this->updateAuthStatus(formatMoltorinoAuthSummary(summary),
                           summary.validAccountCount > 0,
                           summary.validAccountCount == 0);
}

void MoltorinoPage::updateAuthInstructions(const QString &text, bool isError)
{
    this->authInstructionsLabel_->setText(uiText(text));
    this->authInstructionsLabel_->setStyleSheet(
        QString("QLabel { color: %1; }").arg(isError ? "#ffb4a2" : "#9aa0a6"));
}

void MoltorinoPage::updateAuthStatus(const QString &text, bool isValid,
                                     bool isError)
{
    this->authStatusLabel_->setText(uiText(text));

    QString color = "#9aa0a6";
    if (isValid)
    {
        color = "#47d16c";
    }
    else if (isError)
    {
        color = "#ff7b72";
    }

    this->authStatusLabel_->setStyleSheet(
        QString("QLabel { color: %1; }").arg(color));
}

void MoltorinoPage::revealBotBadgeSettings(bool revealed)
{
    this->botBadgeUnlocked_ = revealed;
    this->botBadgeFrame_->setVisible(this->botBadgeUnlocked_);
}

void MoltorinoPage::populateBotBadgeFieldsFromSettings()
{
    const auto &settings = *getSettings();
    this->botBadgeClientIdEdit_->setText(settings.botBadgeClientID);
    this->botBadgeClientSecretEdit_->setText(settings.botBadgeClientSecret);
    this->botBadgeSenderEdit_->setText(settings.botBadgeUserLogin);

    const auto displayName = settings.botBadgeUserName.getValue().trimmed();
    const auto login = settings.botBadgeUserLogin.getValue().trimmed();
    const auto userId = settings.botBadgeUserID.getValue().trimmed();
    const auto appExpiry = settings.botBadgeAppTokenExpiry.getValue().trimmed();

    if (!userId.isEmpty())
    {
        this->botBadgeIdentityLabel_->setText(
            uiText(QString("Bot badge account: %1 (@%2) - ID %3\nToken expires: %4"))
                .arg(displayName.isEmpty() ? login : displayName,
                     login.isEmpty() ? QString("unknown") : login, userId,
                     appExpiry.isEmpty() ? QString("not issued")
                                         : formatTimestampStatus(appExpiry)));
    }
    else
    {
        this->botBadgeIdentityLabel_->setText(
            uiText("No bot badge account verified yet."));
    }

    this->updateBotBadgeStatus(
        settings.botBadgeAppAccessToken.getValue().trimmed().isEmpty()
            ? uiText("Bot badge panel visible. Click Verify to test the setup.")
            : uiText("Bot badge setup saved. Click Verify to check it."),
        false);
}

void MoltorinoPage::updateBotBadgeStatus(const QString &text, bool isValid,
                                         bool isError)
{
    this->botBadgeStatusLabel_->setText(uiText(text));

    QString color = "#9aa0a6";
    if (isValid)
    {
        color = "#47d16c";
    }
    else if (isError)
    {
        color = "#ff7b72";
    }

    this->botBadgeStatusLabel_->setStyleSheet(
        QString("QLabel { color: %1; }").arg(color));
}

void MoltorinoPage::openBotBadgeAuthorization()
{
    const auto clientId = this->botBadgeClientIdEdit_->text().trimmed();
    if (clientId.isEmpty())
    {
        this->revealBotBadgeSettings(true);
        this->updateBotBadgeStatus(
            uiText("Client ID is required before opening Twitch authorization."), false,
            true);
        return;
    }

    QUrl url("https://id.twitch.tv/oauth2/authorize");
    QUrlQuery query;
    query.addQueryItem("response_type", "token");
    query.addQueryItem("client_id", clientId);
    query.addQueryItem("redirect_uri", "http://localhost/");
    query.addQueryItem("scope", "user:write:chat user:bot");
    url.setQuery(query);

    if (QDesktopServices::openUrl(url))
    {
        this->updateBotBadgeStatus(
            uiText("Twitch authorization opened. Approve access in the browser, then "
            "come back and click Verify again."));
    }
    else
    {
        this->updateBotBadgeStatus(
            uiText("Could not open Twitch authorization automatically."), false, true);
    }
}

void MoltorinoPage::verifyBotBadgeConfiguration()
{
    const auto clientId = this->botBadgeClientIdEdit_->text().trimmed();
    const auto clientSecret = this->botBadgeClientSecretEdit_->text().trimmed();

    if (clientId.isEmpty() || clientSecret.isEmpty())
    {
        this->updateBotBadgeStatus(uiText("Client ID and Client Secret are required."),
                                   false, true);
        return;
    }

    QString login = this->botBadgeSenderEdit_->text().trimmed().toLower();

    if (login.isEmpty())
    {
        auto account = getApp()->getAccounts()->twitch.getCurrent();
        if (account->isAnon())
        {
            this->updateBotBadgeStatus(
                uiText("You must enter a Bot Username or be logged into Twitch in "
                "Chatterino to verify."),
                false, true);
            return;
        }
        login = account->getUserName();
    }

    const int generation = ++this->botBadgeValidationGeneration_;
    QPointer<MoltorinoPage> guard(this);

    auto setBusy = [this](bool busy) {
        this->botBadgeIsValidating_ = busy;
    };

    setBusy(true);
    this->updateBotBadgeStatus(
        uiText(QString("Checking bot badge account @%1...")).arg(login));

    QUrl tokenUrl("https://id.twitch.tv/oauth2/token");
    QUrlQuery tokenQuery;
    tokenQuery.addQueryItem("client_id", clientId);
    tokenQuery.addQueryItem("client_secret", clientSecret);
    tokenQuery.addQueryItem("grant_type", "client_credentials");
    tokenUrl.setQuery(tokenQuery);

    NetworkRequest(tokenUrl, NetworkRequestType::Post)
        .hideRequestBody()
        .timeout(20000)
        .onSuccess([guard, generation, clientId, clientSecret, login,
                    setBusy](const NetworkResult &appRes) {
            if (guard == nullptr ||
                generation != guard->botBadgeValidationGeneration_)
            {
                return;
            }

            const auto json = appRes.parseJson();
            const auto appToken =
                json.value("access_token").toString().trimmed();
            const auto appTokenExpiry =
                QDateTime::currentDateTimeUtc()
                    .addSecs(json.value("expires_in").toInt())
                    .toString(Qt::ISODate);

            if (appToken.isEmpty())
            {
                setBusy(false);
                guard->updateBotBadgeStatus(
                    uiText("Could not verify the app. Check the Client Secret."), false,
                    true);
                return;
            }

            QUrl usersUrl("https://api.twitch.tv/helix/users");
            QUrlQuery usersQuery;
            usersQuery.addQueryItem("login", login);
            usersUrl.setQuery(usersQuery);

            NetworkRequest(usersUrl, NetworkRequestType::Get)
                .header("Client-ID", clientId)
                .header("Authorization", "Bearer " + appToken)
                .timeout(20000)
                .onSuccess([guard, generation, clientId, clientSecret, appToken,
                            appTokenExpiry, login,
                            setBusy](const NetworkResult &usersRes) {
                    if (guard == nullptr ||
                        generation != guard->botBadgeValidationGeneration_)
                    {
                        return;
                    }

                    const auto usersJson = usersRes.parseJson();
                    const auto users = usersJson.value("data").toArray();
                    if (users.isEmpty())
                    {
                        setBusy(false);
                        guard->updateBotBadgeStatus(
                            uiText("Bot badge account could not be found."), false,
                            true);
                        return;
                    }

                    const auto user = users.first().toObject();
                    const auto resolvedUserId = user.value("id").toString();
                    const auto resolvedLogin = user.value("login").toString();
                    const auto resolvedDisplayName =
                        user.value("display_name").toString();

                    auto &settings = *getSettings();
                    settings.botBadgeClientID = clientId;
                    settings.botBadgeClientSecret = clientSecret;
                    settings.botBadgeAppAccessToken = appToken;
                    settings.botBadgeAppTokenExpiry = appTokenExpiry;
                    settings.botBadgeUserID = resolvedUserId;
                    settings.botBadgeUserLogin = resolvedLogin;
                    settings.botBadgeUserName = resolvedDisplayName.isEmpty()
                                                    ? resolvedLogin
                                                    : resolvedDisplayName;
                    settings.requestSave();

                    guard->populateBotBadgeFieldsFromSettings();
                    guard->updateBotBadgeStatus(
                        uiText(QString("Bot badge account @%1 verified."))
                            .arg(resolvedLogin),
                        true);
                    setBusy(false);
                })
                .onError(
                    [guard, generation, setBusy](const NetworkResult &res) {
                        if (guard == nullptr ||
                            generation != guard->botBadgeValidationGeneration_)
                        {
                            return;
                        }
                        setBusy(false);
                        guard->updateBotBadgeStatus(
                            uiText("Verification failed: ") + res.formatError(), false,
                            true);
                    })
                .execute();
        })
        .onError([guard, generation, setBusy](const NetworkResult &res) {
            if (guard == nullptr ||
                generation != guard->botBadgeValidationGeneration_)
            {
                return;
            }
            const auto json = res.parseJson();
            const auto message = json.value("message").toString();
            setBusy(false);
            guard->updateBotBadgeStatus(
                uiText("Could not verify bot badge setup: ") +
                    (message.isEmpty() ? res.formatError() : message),
                false, true);
        })
        .execute();
}

void MoltorinoPage::hideEvent(QHideEvent *event)
{
    SettingsPage::hideEvent(event);
    this->logoClickCount_ = 0;
    this->revealBotBadgeSettings(false);
}

}  // namespace chatterino
