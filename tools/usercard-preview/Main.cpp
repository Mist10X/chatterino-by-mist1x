// SPDX-License-Identifier: MIT
// This executable is an offline UI fixture, never the normal application.

#include "common/Channel.hpp"
#include "common/UiLanguage.hpp"
#include "common/network/NetworkManager.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/spellcheck/SpellChecker.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "common/LinkParser.hpp"
#include "providers/twitch/ClipPreview.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageThread.hpp"
#include "providers/twitch/IrcMessageHandler.hpp"
#include "widgets/helper/HighlightToast.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "OwnTimeoutChecks.hpp"
#include "LinkPreviewChecks.hpp"
#include "PublicLogsChecks.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "singletons/ImageUploader.hpp"
#include "singletons/Resources.hpp"
#include "singletons/WindowManager.hpp"
#ifdef LEAFYRINO_BASELINE_USERCARD_PREVIEW
#    include "BaselineUserInfoPopup.hpp"
#else
#    include "HistoryChecks.hpp"
#    include "widgets/dialogs/UserInfoPopup.hpp"
#endif
#include "widgets/helper/ChannelView.hpp"
#include "widgets/helper/CornerBanners.hpp"
#include "widgets/Label.hpp"
#include "widgets/splits/Split.hpp"

#include <IrcMessage>
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStyleFactory>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstdio>

#ifdef Q_OS_WIN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <Windows.h>
#endif

namespace {

using namespace chatterino;

#ifdef LEAFYRINO_BASELINE_USERCARD_PREVIEW
using PreviewPopup = BaselineUserInfoPopup;
using PreviewData = BaselineUserInfoPreviewData;
#else
using PreviewPopup = UserInfoPopup;
using PreviewData = UserInfoPreviewData;
#endif

struct GuiLifecycleGuard {
    ~GuiLifecycleGuard()
    {
        // Also covers argument validation failures before exec() was entered.
        markAppAboutToQuit();
    }
};

bool clipDemo = false;  // --clip-demo: last message carries a clip link

class OfflineApplication final : public mock::BaseApplication
{
public:
    OfflineApplication()
        : windows(this->args, this->paths_, this->settings, this->theme,
                  this->fonts)
        , commands(this->paths_)
    {
        // No AccountController::load(), provider initialize(), NetworkManager,
        // credential store, Twitch IRC, EventSub, or PLUS connection is started.
        this->settings.disableSave();
        this->settings.alwaysLoadMoreUsercardMessages = false;
        this->settings.scrollbackSplitLimit = 6000;
        this->settings.scrollbackUsercardLimit = 6000;
        this->settings.showUsercardSevenTVPaint = false;
        this->settings.showFollowButtonInUsercard = false;
        this->settings.showPronouns = true;
    }

    WindowManager *getWindows() override
    {
        return &this->windows;
    }
    AccountController *getAccounts() override
    {
        return &this->accounts;
    }
    HotkeyController *getHotkeys() override
    {
        return &this->hotkeys;
    }
    CommandController *getCommands() override
    {
        return &this->commands;
    }
    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }
    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }
    IUserDataController *getUserData() override
    {
        return &this->userData;
    }
    SeventvPaints *getSeventvPaints() override
    {
        return &this->paints;
    }
    BttvEmotes *getBttvEmotes() override
    {
        return &this->bttv;
    }
    FfzEmotes *getFfzEmotes() override
    {
        return &this->ffz;
    }
    SeventvEmotes *getSeventvEmotes() override
    {
        return &this->seventv;
    }
    SpellChecker *getSpellChecker() override
    {
        return &this->spellChecker;
    }
    ILinkResolver *getLinkResolver() override
    {
        return &this->linkResolver;
    }
    PubSub *getTwitchPubSub() override
    {
        // Never connected: nothing in this fixture asks it to listen.
        return &this->pubSub;
    }
    ImageUploader *getImageUploader() override
    {
        // Nothing is uploaded: the checks only drive its progress signal.
        return &this->imageUploader;
    }

    WindowManager windows;
    AccountController accounts;
    HotkeyController hotkeys;
    CommandController commands;
    mock::EmoteController emotes;
    mock::MockTwitchIrcServer twitch;
    mock::UserDataController userData;
    SeventvPaints paints;
    BttvEmotes bttv;
    FfzEmotes ffz;
    SeventvEmotes seventv;
    SpellChecker spellChecker;
    PubSub pubSub{QStringLiteral("wss://127.0.0.1:9")};
    LinkResolver linkResolver;
    ImageUploader imageUploader;
};

PreviewData fixture(const QString &scenario)
{
    const bool secondIdentity = scenario == QStringLiteral("b");
    const bool empty = scenario == QStringLiteral("empty");
    const bool longHistory = scenario == QStringLiteral("long");
    PreviewData data;
    data.login = QStringLiteral("archive_demo");
    data.userId = secondIdentity ? QStringLiteral("910000002")
                  : empty        ? QStringLiteral("910000003")
                                 : QStringLiteral("910000001");
    data.displayName = secondIdentity ? QStringLiteral("Archive_Demo B")
                                      : QStringLiteral("Archive_Demo A");
    if (empty)
    {
        data.displayName = QStringLiteral("Archive_Demo • empty");
    }
    data.createdText = QStringLiteral("Создан 2021-04-15 · тестовые данные");
    data.followersText = QStringLiteral("Фолловеров 1 234 · тестовые данные");
    data.chatColor = QStringLiteral("#E07700");
    data.pronouns = QStringLiteral("Местоимения: they/them · тестовые данные");
    data.avatar = QPixmap(128, 128);
    data.avatar.fill(QColor(secondIdentity ? "#355b77" : "#67418b"));
    {
        QPainter painter(&data.avatar);
        painter.setPen(Qt::white);
        auto font = painter.font();
        font.setPixelSize(60);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(
            data.avatar.rect(), Qt::AlignCenter,
            secondIdentity ? QStringLiteral("B") : QStringLiteral("A"));
    }
    data.banner = QPixmap(760, 240);
    data.banner.fill(QColor(secondIdentity ? "#1d4555" : "#35264b"));
    {
        QPainter painter(&data.banner);
        painter.setPen(QColor("#a98acb"));
        for (int x = -240; x < data.banner.width(); x += 60)
        {
            painter.drawLine(x, 240, x + 240, 0);
        }
    }
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
    // Eight local fixture badges exercise the earned collection, with no image
    // downloads or account lookup. These are visibly synthetic test icons.
    // Image::fromResourcePixmap caches by the source object's address. Keep
    // eight distinct source objects alive instead of reusing one stack slot.
    static const std::array<QPixmap, 8> badgePixmaps = [] {
        std::array<QPixmap, 8> icons;
        for (int i = 0; i < 8; ++i)
        {
            auto &icon = icons[size_t(i)];
            icon = QPixmap(36, 36);
            icon.fill(Qt::transparent);
            QPainter painter(&icon);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor::fromHsv(i * 43, 155, 245));
            painter.drawRoundedRect(3, 3, 30, 30, 7, 7);
            painter.setPen(Qt::white);
            auto font = painter.font();
            font.setPixelSize(22);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(icon.rect(), Qt::AlignCenter,
                             QString::number(i + 1));
        }
        return icons;
    }();
    for (int i = 0; i < 8; ++i)
    {
        data.earnedBadges.push_back(std::make_shared<Emote>(Emote{
            .name = EmoteName{QStringLiteral("Synthetic badge %1").arg(i + 1)},
            .images = ImageSet{Image::fromResourcePixmap(
                badgePixmaps[size_t(i)], .5)},
            .tooltip =
                Tooltip{QStringLiteral("Synthetic earned badge %1").arg(i + 1)},
            .id = EmoteId{QStringLiteral("fixture-earned-%1").arg(i + 1)},
        }));
    }
#endif
    // Type::None has no logger or 7TV lookups. This is still the real Channel
    // queue and the real ChannelView message renderer used by UserInfoPopup.
    data.messages = std::make_shared<Channel>(QStringLiteral("archive_room"),
                                              Channel::Type::None);
    const auto count = empty ? 0 : longHistory ? 5000 : 18;
    const QStringList texts{
        QStringLiteral("Это синтетическое сообщение для проверки карточки."),
        QStringLiteral(
            "Русский и English text stay readable in the same line."),
        QStringLiteral("Сохранённый текст является контекстом, а не доказанной "
                       "причиной наказания."),
        QStringLiteral("Очень длинная строка: ") + QString(380, QChar(0x044F)),
        QStringLiteral(
            "<b>Это обычный текст, а не HTML.</b> & quote \"example\""),
        QStringLiteral("Reply example: ответ на ранее сохранённое сообщение.")};
    const auto start = QDateTime::fromString(
        QStringLiteral("2026-10-20T12:00:00Z"), Qt::ISODate);
    QPixmap emotePixmap(28, 28);
    emotePixmap.fill(Qt::transparent);
    {
        QPainter painter(&emotePixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#b397f4"));
        painter.drawEllipse(1, 1, 26, 26);
        painter.setBrush(QColor("#21182d"));
        painter.drawEllipse(7, 8, 4, 5);
        painter.drawEllipse(17, 8, 4, 5);
        painter.drawRoundedRect(8, 18, 12, 3, 1, 1);
    }
    const auto emote = std::make_shared<Emote>(Emote{
        .name = EmoteName{QStringLiteral("PreviewSmile")},
        .images = ImageSet{Image::fromResourcePixmap(emotePixmap)},
        .tooltip = {},
        .homePage = {},
        .id = {},
        .author = {},
        .baseName = {},
    });
    MessagePtr parent;
    std::shared_ptr<MessageThread> thread;
    for (int i = 0; i < count; ++i)
    {
        MessageBuilder builder;
        auto &message = builder.message();
        message.id = QStringLiteral("synthetic-%1-%2").arg(data.userId).arg(i);
        message.userID = data.userId;
        message.loginName = data.login;
        message.displayName = data.displayName;
        message.channelName = QStringLiteral("archive_room");
        message.serverReceivedTime = start.addSecs(i * 17);
        message.messageText = texts.at(i % texts.size());
        if (!secondIdentity && i == 17)
        {
            // Exact match to the independent saved Archive fixture. This lets
            // the real message-context action navigate without live services.
            message.id = QStringLiteral("demo-30");
            message.serverReceivedTime =
                QDateTime::fromMSecsSinceEpoch(1792440030000LL, Qt::UTC);
            message.messageText =
                QStringLiteral("Синтетическая история · сообщение 30");
        }
        if (i % 6 == 5 && parent)
        {
            message.replyParent = parent;
            message.replyThread = thread;
            builder.emplace<ReplyCurveElement>();
            builder.emplace<TextElement>(
                QStringLiteral("Replying to Archive_Demo"),
                MessageElementFlag::RepliedMessage, MessageColor::System);
            builder.emplace<LinebreakElement>(
                MessageElementFlag::RepliedMessage);
        }
        builder.emplace<TimestampElement>(message.serverReceivedTime.time());
        builder.emplace<TextElement>(data.displayName + QStringLiteral(": "),
                                     MessageElementFlag::Username,
                                     QColor(QStringLiteral("#b397f4")));
        builder.emplace<TextElement>(message.messageText,
                                     MessageElementFlag::Text);
        if (i % 3 == 2)
        {
            builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);
        }
        if (clipDemo && i == count - 1)
        {
            QPixmap thumb(480, 272);
            {
                QPainter painter(&thumb);
                QLinearGradient gradient(0, 0, 480, 272);
                gradient.setColorAt(0, QColor("#3b6d3a"));
                gradient.setColorAt(1, QColor("#1d2b4a"));
                painter.fillRect(thumb.rect(), gradient);
            }
            clippreview::insertForTesting({
                .slug = QStringLiteral("FakeClipDemo"),
                .url = QStringLiteral(
                    "https://clips.twitch.tv/FakeClipDemo"),
                .title = QStringLiteral("Synthetic clip title"),
                .broadcaster = QStringLiteral("morphe_ya"),
                .creator = QStringLiteral("Deux_Visages"),
                .gameName = QStringLiteral("Spore"),
                .thumbnail = Image::fromResourcePixmap(thumb),
                .views = 1,
                .durationSeconds = 6,
                .createdAt = QDateTime::currentDateTimeUtc(),
            });
            const QString link = QStringLiteral(
                "https://www.twitch.tv/morphe_ya/clip/FakeClipDemo");
            builder.addLink(*linkparser::parse(link), link);
        }
        auto built = builder.release();
        if (!parent)
        {
            parent = built;
            thread = std::make_shared<MessageThread>(parent);
        }
        else if (built->replyThread)
        {
            thread->addToThread(MessagePtr{built});
        }
        data.messages->addMessage(std::move(built), MessageContext::Repost);
    }
    return data;
}

QScreen *previewScreen()
{
    for (auto *screen : QGuiApplication::screens())
    {
        if (screen != QGuiApplication::primaryScreen())
        {
            return screen;
        }
    }
    return nullptr;
}

QJsonObject nativeHitTests(PreviewPopup &popup)
{
    QJsonObject result;
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
    {
        return result;
    }
    auto *tabs =
        popup.findChild<QTabWidget *>(QStringLiteral("UsercardContent"));
    auto *profile =
        popup.findChild<QScrollArea *>(QStringLiteral("UsercardProfileScroll"));
    if (!tabs || !profile)
    {
        return result;
    }
    const auto window = reinterpret_cast<HWND>(popup.winId());
    DWORD ownerProcess = 0;
    if (!IsWindow(window) ||
        GetWindowThreadProcessId(window, &ownerProcess) == 0 ||
        ownerProcess != GetCurrentProcessId())
    {
        return result;
    }
    auto checkPoint = [&](const QString &name, QWidget *widget, QPoint point) {
        const auto local = widget->mapTo(&popup, point);
        POINT nativePoint{qRound(local.x() * popup.devicePixelRatioF()),
                          qRound(local.y() * popup.devicePixelRatioF())};
        // Only this process's synthetic popup HWND is queried. No input is
        // injected and no other window or process is inspected.
        ClientToScreen(window, &nativePoint);
        const auto hit = SendMessageW(window, WM_NCHITTEST, 0,
                                      MAKELPARAM(nativePoint.x, nativePoint.y));
        result[name] = int(hit);
    };
    if (auto *toggle =
            popup.findChild<QWidget *>(QStringLiteral("UsercardArchiveToggle")))
    {
        checkPoint(QStringLiteral("archive-link"), toggle,
                   toggle->rect().center());
    }
    checkPoint(QStringLiteral("content-page"), tabs->currentWidget(),
               tabs->currentWidget()->rect().center());
    checkPoint(QStringLiteral("profile-viewport"), profile->viewport(),
               profile->viewport()->rect().center());
#else
    (void)popup;
#endif
    return result;
}

bool writeReport(const QString &path, const OfflineApplication &app,
                 const PreviewData &data, const PreviewPopup &popup,
                 const QJsonArray &checks)
{
    auto *tabs =
        popup.findChild<QTabWidget *>(QStringLiteral("UsercardContent"));
    auto *profile =
        popup.findChild<QScrollArea *>(QStringLiteral("UsercardProfileScroll"));
    QJsonObject result{
        {QStringLiteral("mode"), QStringLiteral("offline-actual-usercard")},
#ifdef LEAFYRINO_BASELINE_USERCARD_PREVIEW
        {QStringLiteral("layout"),
         QStringLiteral("baseline-source-offline-seam")},
#else
        {QStringLiteral("layout"),
         QStringLiteral("reference-usercard-stage-2")},
#endif
        {QStringLiteral("normalApplicationInitialized"), false},
        {QStringLiteral("networkManagerInitialized"),
         NetworkManager::workerThread != nullptr},
        {QStringLiteral("accountsLoaded"), false},
        {QStringLiteral("qtRuntime"), QString::fromLatin1(qVersion())},
        {QStringLiteral("autoClose"),
         popup.property("previewAutoClose").toBool()},
        {QStringLiteral("mouseTracking"),
         QJsonObject{{QStringLiteral("tabs"), tabs && tabs->hasMouseTracking()},
                     {QStringLiteral("tabBar"),
                      tabs && tabs->tabBar()->hasMouseTracking()},
                     {QStringLiteral("contentPage"),
                      tabs && tabs->currentWidget()->hasMouseTracking()},
                     {QStringLiteral("profileScroll"),
                      profile && profile->hasMouseTracking()},
                     {QStringLiteral("profileViewport"),
                      profile && profile->viewport()->hasMouseTracking()}}},
        {QStringLiteral("userId"), data.userId},
        {QStringLiteral("login"), data.login},
        {QStringLiteral("messageCount"),
         int(data.messages->getMessageSnapshot().size())},
        {QStringLiteral("width"), popup.width()},
        {QStringLiteral("height"), popup.height()},
        {QStringLiteral("devicePixelRatio"), popup.devicePixelRatioF()},
        {QStringLiteral("settings"), app.settingsDir.path()},
        {QStringLiteral("checks"), checks},
    };
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(QJsonDocument(result).toJson(QJsonDocument::Indented)) >
               0 &&
           file.commit();
}

}  // namespace

int main(int argc, char **argv)
{
    // Reject accidental launches from either real installation. No profile is
    // constructed or opened before checking the executable's own directory.
    const QString exeDir =
        QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteDir().absolutePath();
    const QString allowedRoot = QStringLiteral("D:/devtools/");
    if (!exeDir.startsWith(allowedRoot, Qt::CaseInsensitive))
    {
        return 20;
    }
    const auto temporaryRoot = exeDir + QStringLiteral("/preview-temporary");
    if (!QDir().mkpath(temporaryRoot))
    {
        return 21;
    }
    // Existing test helpers create a QTemporaryDir. Keep even that directory
    // inside this standalone test copy, without modifying the system env.
    qputenv("TEMP", temporaryRoot.toLocal8Bit());
    qputenv("TMP", temporaryRoot.toLocal8Bit());
    QApplication qapp(argc, argv);
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen")
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    GuiLifecycleGuard lifecycleGuard;
    qapp.setApplicationName(QStringLiteral("Leafyrino-usercard-preview"));
    qapp.setOrganizationName(QStringLiteral("LeafyrinoSyntheticPreview"));
    qapp.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QObject::connect(&qapp, &QApplication::aboutToQuit, &qapp,
                     &markAppAboutToQuit);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("case")},
         QStringLiteral("a, b, empty, or long"),
         QStringLiteral("case"),
         QStringLiteral("a")},
        {{QStringLiteral("archive-socket")},
         QStringLiteral("Explicit synthetic local socket"),
         QStringLiteral("socket")},
        {{QStringLiteral("archive-dataset")},
         QStringLiteral("Expected synthetic dataset"),
         QStringLiteral("dataset")},
        {{QStringLiteral("width")},
         QStringLiteral("Card width in logical pixels"),
         QStringLiteral("width"),
         QStringLiteral("860")},
        {{QStringLiteral("height")},
         QStringLiteral("Card height in logical pixels"),
         QStringLiteral("height"),
         QStringLiteral("420")},
        {{QStringLiteral("scale")},
         QStringLiteral("Widget scale override"),
         QStringLiteral("scale"),
         QStringLiteral("1.0")},
        {{QStringLiteral("report")},
         QStringLiteral("Write fixture-only diagnostics JSON"),
         QStringLiteral("path")},
        {{QStringLiteral("screenshot")},
         QStringLiteral("Save this fixture widget's own rendered PNG (not an "
                        "OS screenshot)"),
         QStringLiteral("path")},
        {{QStringLiteral("banner-demo")},
         QStringLiteral("Show a raid and a mass-gift corner banner on the "
                        "card's chat view (synthetic, no avatar download)")},
        {{QStringLiteral("show-moderation")},
         QStringLiteral("Show the moderation strip as for a moderator")},
        {{QStringLiteral("toast-demo")},
         QStringLiteral("Render two highlight cards on a plain widget")},
        {{QStringLiteral("own-timeout-checks")},
         QStringLiteral("Drive the own-timeout input states on a synthetic "
                        "channel and exit")},
        {{QStringLiteral("clip-demo")},
         QStringLiteral("Last synthetic message carries a clip link with a "
                        "ready card (no network)")},
        {{QStringLiteral("link-preview-checks")},
         QStringLiteral("Run the link-preview checks against a local stand-in "
                        "resolver and exit")},
        {{QStringLiteral("public-logs-checks")},
         QStringLiteral("Run the public chat log checks against local stand-in "
                        "mirrors and exit")},
        {{QStringLiteral("public-logs-live")},
         QStringLiteral("One real page from the public mirrors: "
                        "user-id,login,channel (network!)"),
         QStringLiteral("who")},
        {{QStringLiteral("viewer-counts-live")},
         QStringLiteral("Real viewer counts for logins a,b,c (network!)"),
         QStringLiteral("logins")},
        {{QStringLiteral("show-archive")},
         QStringLiteral("Open the Archive page before the screenshot")},
        {{QStringLiteral("render-hidden")},
         QStringLiteral("Render with the native platform without showing "
                        "fixture windows")},
        {{QStringLiteral("history-checks")},
         QStringLiteral("Exercise actual history widgets against a "
                        "process-owned synthetic local server")},
        {{QStringLiteral("scripted-checks")},
         QStringLiteral(
             "Exercise A-B-A, empty/long and responsive actual widgets")},
        {{QStringLiteral("auto-close")},
         QStringLiteral("Use the actual auto-closing frameless popup variant")},
        {{QStringLiteral("native-hit-test-checks")},
         QStringLiteral("Query only this synthetic popup's Windows hit tests")},
        {{QStringLiteral("pending-shutdown")},
         QStringLiteral("Use a private silent local fixture to test shutdown "
                        "during a pending request")},
        {{QStringLiteral("quit-after")},
         QStringLiteral("Exit the test copy after milliseconds"),
         QStringLiteral("milliseconds")},
    });
    parser.process(qapp);
    clipDemo = parser.isSet(QStringLiteral("clip-demo"));
    const auto scenario = parser.value(QStringLiteral("case"));
    if (!QStringList{QStringLiteral("a"), QStringLiteral("b"),
                     QStringLiteral("empty"), QStringLiteral("long")}
             .contains(scenario))
    {
        return 22;
    }
    auto *screen = previewScreen();
    if (!screen &&
        qEnvironmentVariable("QT_QPA_PLATFORM") != QStringLiteral("offscreen"))
    {
        // Never fall back to the user's primary monitor for visible tests.
        return 23;
    }
    initResources();
    initializeUiLanguage(qEnvironmentVariable("LEAFYRINO_PREVIEW_LANGUAGE", "ru"));
    auto appOwner = std::make_unique<OfflineApplication>();
    auto &app = *appOwner;
    if (clipDemo)
    {
        app.settings.showLinkTitles = false;  // offline: no resolver request
    }
    if (parser.isSet(QStringLiteral("link-preview-checks")))
    {
        QJsonArray checks;
        const int failed = linkpreview::runLinkPreviewChecks(app.settings, checks);
        QFile out(parser.value(QStringLiteral("report")));
        if (out.open(QIODevice::WriteOnly))
        {
            out.write(QJsonDocument(QJsonObject{{"mode", "link-preview"},
                                                {"checks", checks}})
                          .toJson());
        }
        return failed == 0 ? 0 : 30;
    }
    if (parser.isSet(QStringLiteral("public-logs-checks")))
    {
        QJsonArray checks;
        const int failed = publiclogchecks::runPublicLogChecks(checks);
        QFile out(parser.value(QStringLiteral("report")));
        if (out.open(QIODevice::WriteOnly))
        {
            out.write(QJsonDocument(QJsonObject{{"mode", "public-logs"},
                                                {"checks", checks}})
                          .toJson());
        }
        return failed == 0 ? 0 : 32;
    }
    if (parser.isSet(QStringLiteral("viewer-counts-live")))
    {
        NetworkManager::init();
        std::optional<QHash<QString, unsigned>> counts;
        chatterino::TwitchGql::getViewerCounts(
            parser.value(QStringLiteral("viewer-counts-live")).split(','),
            [&](QHash<QString, unsigned> value) {
                counts = value;
            });
        publiclogchecks::waitUntil([&] {
            return counts.has_value();
        }, 20000);
        QJsonObject result;
        if (counts)
        {
            for (auto it = counts->begin(); it != counts->end(); ++it)
            {
                result.insert(it.key(), double(it.value()));
            }
        }
        QFile out(parser.value(QStringLiteral("report")));
        if (out.open(QIODevice::WriteOnly))
        {
            out.write(QJsonDocument(QJsonObject{{"answered", counts.has_value()},
                                                {"counts", result}})
                          .toJson());
        }
        return counts ? 0 : 35;
    }
    if (parser.isSet(QStringLiteral("public-logs-live")))
    {
        const auto who = parser.value(QStringLiteral("public-logs-live")).split(',');
        if (who.size() != 3)
        {
            return 33;
        }
        NetworkManager::init();
        QJsonArray pages;
        QJsonObject cursor;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 2; ++i)
        {
            std::optional<QJsonObject> result;
            chatterino::publiclogs::requestPage(
                who[0], who[1], who[2], cursor, nullptr,
                [&](const QJsonObject &value) {
                    result = value;
                });
            publiclogchecks::waitUntil([&] {
                return result.has_value();
            }, 40000);
            if (!result)
            {
                break;
            }
            auto rows = result->value("rows").toArray();
            QJsonObject summary{{"status", result->value("status")},
                                {"error", result->value("error")},
                                {"more", result->value("more")},
                                {"rows", rows.size()},
                                {"ms", double(timer.elapsed())}};
            if (!rows.isEmpty())
            {
                summary.insert("newest", rows.first().toObject().value("time_utc"));
                summary.insert("oldest", rows.last().toObject().value("time_utc"));
            }
            pages.append(summary);
            cursor = result->value("next_cursor").toObject();
            if (!result->value("more").toBool())
            {
                break;
            }
        }
        QFile out(parser.value(QStringLiteral("report")));
        if (out.open(QIODevice::WriteOnly))
        {
            out.write(QJsonDocument(QJsonObject{{"mode", "public-logs-live"},
                                                {"pages", pages}})
                          .toJson());
        }
        return pages.isEmpty() ? 34 : 0;
    }
    auto socket = parser.value(QStringLiteral("archive-socket"));
    const auto dataset = parser.value(QStringLiteral("archive-dataset"));
    const bool historyChecks = parser.isSet(QStringLiteral("history-checks"));
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
    std::unique_ptr<preview::HistoryFixtureServer> historyServer;
    if (historyChecks)
    {
        if (parser.isSet(QStringLiteral("pending-shutdown")))
        {
            return 26;
        }
        historyServer = std::make_unique<preview::HistoryFixtureServer>();
        if (!historyServer->listening())
        {
            return 27;
        }
        socket = historyServer->socketName();
    }
#else
    if (historyChecks)
    {
        return 26;
    }
#endif
    std::unique_ptr<QLocalServer> pendingServer;
    if (parser.isSet(QStringLiteral("pending-shutdown")))
    {
        socket = QStringLiteral("LeafyrinoArchive-test-pending-%1")
                     .arg(QCoreApplication::applicationPid());
        pendingServer = std::make_unique<QLocalServer>();
        pendingServer->setSocketOptions(QLocalServer::UserAccessOption);
        if (!pendingServer->listen(socket))
        {
            return 25;
        }
        QObject::connect(
            pendingServer.get(), &QLocalServer::newConnection,
            pendingServer.get(), [server = pendingServer.get()] {
                while (auto *connection = server->nextPendingConnection())
                {
                    // An intentionally silent, process-owned local endpoint. No
                    // real Archive service or TCP connection is involved.
                    connection->setParent(server);
                    QObject::connect(connection, &QLocalSocket::disconnected,
                                     connection, &QObject::deleteLater);
                }
            });
    }
    if (!socket.isEmpty() &&
        (!socket.startsWith(QStringLiteral("LeafyrinoArchive-test-")) ||
         (!dataset.isEmpty() &&
          dataset != QStringLiteral("leafyrino-card-demo-v1"))))
    {
        return 24;
    }
    // Empty endpoint means unavailable; it must never default to the real
    // Archive service in this fixture executable.
    QStringSetting("/archive/socketName") =
        socket.isEmpty() ? QStringLiteral("LeafyrinoArchive-test-unavailable")
                         : socket;
    QStringSetting("/archive/datasetId") =
        dataset.isEmpty() ? QStringLiteral("leafyrino-card-demo-v1") : dataset;
    auto splitOwner = std::make_unique<Split>(nullptr);
    auto &split = *splitOwner;
    if (parser.isSet(QStringLiteral("toast-demo")))
    {
        QWidget host;
        host.resize(900, 300);
        host.setStyleSheet(QStringLiteral("background:#18171c;"));
        host.show();
        auto make = [](const QString &login, const QString &text, QColor color) {
            auto message = std::make_shared<Message>();
            message->loginName = login.toLower();
            message->displayName = login;
            message->messageText = text;
            message->usernameColor = color;
            return MessagePtr(message);
        };
        showHighlightToastOn(&host, QStringLiteral("morphe_ya"),
                             make(QStringLiteral("Some_Viewer"),
                                  QStringLiteral("@Mist1X_X glad to see you here, synthetic text"),
                                  QColor("#ff7f50")));
        showHighlightToastOn(&host, QStringLiteral("dangerlyoha"),
                             make(QStringLiteral("Another_One"),
                                  QStringLiteral("mist1x what do you think about this very long synthetic message that should be cut"),
                                  QColor("#1e90ff")));
        QCoreApplication::processEvents();
        host.grab().save(parser.value(QStringLiteral("screenshot")), "PNG");
        return 0;
    }
    if (parser.isSet(QStringLiteral("own-timeout-checks")))
    {
        const int failed = owntimeout::runOwnTimeoutChecks(
            split, parser.value(QStringLiteral("report")),
            parser.value(QStringLiteral("screenshot")));
        splitOwner.reset();
        return failed == 0 ? 0 : 31;
    }
    auto controlsOwner = std::make_unique<QWidget>();
    auto &controls = *controlsOwner;
    controls.setObjectName(QStringLiteral("UsercardPreviewControls"));
    controls.setAttribute(Qt::WA_DontShowOnScreen,
                          parser.isSet(QStringLiteral("render-hidden")));
    controls.setWindowTitle(
        QStringLiteral("Leafyrino offline synthetic preview controls"));
    controls.setStyleSheet(QStringLiteral(
        "QWidget#UsercardPreviewControls {background:#17151d;color:#ddd;}"));
    auto *layout = new QVBoxLayout(&controls);
    layout->addWidget(new QLabel(QStringLiteral(
        "Actual UserInfoPopup • synthetic only • no Twitch / PLUS")));
    auto *row = new QHBoxLayout;
    layout->addLayout(row);
    const bool autoClose = parser.isSet(QStringLiteral("auto-close"));
    auto popupOwner = std::make_unique<PreviewPopup>(autoClose, &split, true);
    QPointer<PreviewPopup> popupGuard(popupOwner.get());
    auto &popup = *popupOwner;
    popup.setAttribute(Qt::WA_DontShowOnScreen,
                       parser.isSet(QStringLiteral("render-hidden")));
    popup.setProperty("previewAutoClose", autoClose);
    QObject::connect(&popup, &QObject::destroyed, &controls, [&] {
        // The actual auto-close mode self-deletes on window deactivation.
        // Keep its visible owner alive, as the normal main window would be.
        popupOwner.release();
        for (auto *button : controls.findChildren<QPushButton *>())
        {
            button->setEnabled(false);
        }
        controls.setWindowTitle(
            QStringLiteral("Leafyrino synthetic controls — popup closed"));
    });
    // Native popup ownership must follow a visible window. The hidden Split
    // still supplies the real ChannelView context, but cannot own a visible
    // transient window reliably on Windows.
    popup.setParent(&controls, popup.windowFlags());
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
    if (auto *messagesView = popup.findChild<ChannelView *>())
    {
        // Exercise the production message-menu builder, but the preview may
        // only navigate to its explicit synthetic local Archive endpoint.
        messagesView->setContextMenuPolicy(Qt::DefaultContextMenu);
        std::ignore = messagesView->messageMenuCreated.connect(
            [](QMenu *menu, const MessageLayoutElement *) {
                auto restrictToArchive = [menu] {
                    for (auto *action : menu->actions())
                    {
                        const bool archive =
                            action->menu() &&
                            action->menu()->title() == uiText(QStringLiteral("Архив"));
                        if (!archive)
                        {
                            action->setEnabled(false);
                        }
                    }
                };
                restrictToArchive();
                // The normal WindowManager hook runs after this signal. Apply
                // the same restriction after all builder hooks have finished.
                QObject::connect(menu, &QMenu::aboutToShow, menu,
                                 restrictToArchive);
                std::fputs("offline preview: production message menu built; "
                           "only Archive enabled\n",
                           stderr);
            });
    }
#endif
    // The popup normally owns itself after close. This fixture owns it on the
    // stack so that the mock application always outlives all of its widgets.
    popup.setAttribute(Qt::WA_DeleteOnClose, false);
    popup.setOverrideScale(std::clamp(
        parser.value(QStringLiteral("scale")).toFloat(), 0.75F, 2.0F));
    auto current = fixture(scenario);
    auto showCase = [&](const QString &next) {
        if (!popupGuard)
        {
            return;
        }
        current = fixture(next);
        popupGuard->setOfflinePreviewData(current);
        popupGuard->show();
    };
    for (const auto &name : {QStringLiteral("a"), QStringLiteral("b"),
                             QStringLiteral("empty"), QStringLiteral("long")})
    {
        auto *button = new QPushButton(name.toUpper(), &controls);
        button->setObjectName(QStringLiteral("preview-case-") + name);
        row->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &controls, [&, name] {
            showCase(name);
        });
    }
    const auto width =
        std::clamp(parser.value(QStringLiteral("width")).toInt(), 480, 1400);
    const auto height =
        std::clamp(parser.value(QStringLiteral("height")).toInt(), 400, 1000);
    if (screen)
    {
        const auto bounds = screen->availableGeometry();
        controls.move(bounds.topLeft() + QPoint(24, 24));
        popup.move(bounds.topLeft() + QPoint(40, 130));
    }
    controls.show();
    showCase(scenario);
    // A queued size restores the requested test geometry after native DPI
    // notifications and the existing popup's scale/layout initialization.
    QTimer::singleShot(180, &popup, [&] {
        popup.setMinimumSize(0, 0);
        popup.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        popup.resize(width, height);
    });
    const auto report = parser.value(QStringLiteral("report"));
    QJsonArray checks;
    bool checksPassed = true;
    if (parser.isSet(QStringLiteral("scripted-checks")) && !historyChecks)
    {
        QTimer::singleShot(400, &popup, [&] {
            auto check = [&](const QString &name, bool ok) {
                checksPassed &= ok;
                checks.append(QJsonObject{{QStringLiteral("name"), name},
                                          {QStringLiteral("ok"), ok}});
            };
            for (const auto &name :
                 {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("a"),
                  QStringLiteral("empty"), QStringLiteral("long")})
            {
                showCase(name);
                bool identityVisible = false;
                for (auto *widget : popup.findChildren<QWidget *>())
                {
                    if (auto *label = dynamic_cast<Label *>(widget))
                    {
                        identityVisible |=
                            label->getText() ==
                            QStringLiteral("ID: ") + current.userId;
                    }
                }
                check(name + QStringLiteral(": actual UID label"),
                      identityVisible);
                auto *view = popup.findChild<ChannelView *>();
                check(name + QStringLiteral(": actual ChannelView data"),
                      view && view->underlyingChannel() == current.messages &&
                          view->channel()->getMessageSnapshot().size() ==
                              size_t(name == QStringLiteral("long")    ? 5000
                                     : name == QStringLiteral("empty") ? 0
                                                                       : 18));
            }
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
            {
                auto mixed = fixture(QStringLiteral("a"));
                auto appendIdentityRow = [&](const QString &uid,
                                             const QString &login) {
                    MessageBuilder builder;
                    auto &message = builder.message();
                    message.id =
                        QStringLiteral("synthetic-mixed-") + uid + login;
                    message.userID = uid;
                    message.loginName = login;
                    message.displayName = login;
                    message.channelName = QStringLiteral("archive_room");
                    message.messageText =
                        QStringLiteral("Identity filtering fixture");
                    message.serverReceivedTime =
                        QDateTime::currentDateTimeUtc();
                    builder.emplace<TextElement>(login + QStringLiteral(": "),
                                                 MessageElementFlag::Username);
                    builder.emplace<TextElement>(message.messageText,
                                                 MessageElementFlag::Text);
                    mixed.messages->addMessage(builder.release(),
                                               MessageContext::Repost);
                };
                appendIdentityRow(QStringLiteral("910000002"), mixed.login);
                appendIdentityRow(mixed.userId,
                                  QStringLiteral("archive_old_login"));
                appendIdentityRow({}, mixed.login);
                mixed.sourceMessages = mixed.messages;
                popup.setOfflinePreviewData(mixed);
                auto *view = popup.findChild<ChannelView *>();
                const auto visible = view
                                         ? view->channel()->getMessageSnapshot()
                                         : std::vector<MessagePtr>{};
                check(QStringLiteral("normal UID filter excludes same-login "
                                     "other UID and includes old login"),
                      visible.size() == 19 &&
                          std::all_of(visible.begin(), visible.end(),
                                      [&](const auto &message) {
                                          return message->userID ==
                                                 mixed.userId;
                                      }) &&
                          std::any_of(visible.begin(), visible.end(),
                                      [](const auto &message) {
                                          return message->loginName ==
                                                 QStringLiteral(
                                                     "archive_old_login");
                                      }));
            }

            {
                // Punishments about the person: shown even with no messages,
                // tied by Twitch ID, never from another channel. The notices
                // go through the real IRC parser (CLEARCHAT / CLEARMSG).
                auto feed = [](const ChannelPtr &sink, const QString &raw) {
                    std::unique_ptr<Communi::IrcMessage> message(
                        Communi::IrcMessage::fromData(raw.toUtf8(), nullptr));
                    IrcMessageHandler::parseMessageInto(message.get(), *sink,
                                                        nullptr);
                };
                auto clearchat = [&](const ChannelPtr &sink,
                                     const QString &login, const QString &uid,
                                     const QString &duration) {
                    QStringList tags{
                        QStringLiteral("room-id=900000001"),
                        QStringLiteral("tmi-sent-ts=%1")
                            .arg(QDateTime::currentMSecsSinceEpoch())};
                    if (!uid.isEmpty())
                    {
                        tags << QStringLiteral("target-user-id=") + uid;
                    }
                    if (!duration.isEmpty())
                    {
                        tags << QStringLiteral("ban-duration=") + duration;
                    }
                    feed(sink, u'@' + tags.join(u';') +
                                   QStringLiteral(" :tmi.twitch.tv CLEARCHAT "
                                                  "#archive_room :") +
                                   login);
                };
                auto notices = [](const std::vector<MessagePtr> &rows) {
                    return std::count_if(rows.begin(), rows.end(),
                                         [](const auto &m) {
                                             return !m->timeoutUser.isEmpty();
                                         });
                };
                auto shown = [&](const PreviewData &data) {
                    popup.setOfflinePreviewData(data);
                    auto *view = popup.findChild<ChannelView *>();
                    return view ? view->channel()->getMessageSnapshot()
                                : std::vector<MessagePtr>{};
                };
                auto person = [&](const QString &login, const QString &uid) {
                    auto data = fixture(QStringLiteral("empty"));
                    data.login = login;
                    data.displayName = login;
                    data.userId = uid;
                    data.messages = std::make_shared<Channel>(
                        QStringLiteral("archive_room"), Channel::Type::None);
                    data.sourceMessages = data.messages;
                    return data;
                };

                // 1. messages, then a ban and one deleted message
                auto writer = fixture(QStringLiteral("a"));
                const auto own =
                    writer.messages->getMessageSnapshot().back()->id;
                feed(writer.messages,
                     QStringLiteral("@login=archive_demo;target-msg-id=") +
                         own +
                         QStringLiteral(" :tmi.twitch.tv CLEARMSG "
                                        "#archive_room :deleted text"));
                clearchat(writer.messages, writer.login, writer.userId, {});
                clearchat(writer.messages, QStringLiteral("someone_else"),
                          QStringLiteral("920000001"), {});
                feed(writer.messages,
                     QStringLiteral("@room-id=900000001 :tmi.twitch.tv "
                                    "CLEARCHAT #archive_room"));
                writer.sourceMessages = writer.messages;
                auto rows = shown(writer);
                check(QStringLiteral("messages plus own ban and deletion, "
                                     "not other people's or chat clear"),
                      notices(rows) == 2 &&
                          std::any_of(rows.begin(), rows.end(),
                                      [](const auto &m) {
                                          return m->messageText.contains(
                                              QStringLiteral(
                                                  "permanently banned"));
                                      }) &&
                          rows.size() == size_t(18 + 2));

                // 2. no messages, banned
                auto silent = person(QStringLiteral("silent_user"),
                                     QStringLiteral("910000006"));
                clearchat(silent.messages, silent.login, silent.userId, {});
                rows = shown(silent);
                check(QStringLiteral("ban shown without any messages"),
                      rows.size() == 1 && notices(rows) == 1 &&
                          rows.front()->messageText.contains(
                              QStringLiteral("silent_user has been "
                                             "permanently banned")));

                // 6. reopening does not duplicate
                rows = shown(silent);
                check(QStringLiteral("reopening shows the ban once"),
                      rows.size() == 1);

                // 3. no messages, timed out
                auto muted = person(QStringLiteral("muted_user"),
                                    QStringLiteral("910000007"));
                clearchat(muted.messages, muted.login, muted.userId,
                          QStringLiteral("600"));
                rows = shown(muted);
                check(QStringLiteral("timeout shown without any messages"),
                      rows.size() == 1 &&
                          rows.front()->messageText.contains(
                              QStringLiteral("has been timed out")));

                // 4. nothing at all
                auto nobody = person(QStringLiteral("nobody_user"),
                                     QStringLiteral("910000008"));
                clearchat(nobody.messages, QStringLiteral("someone_else"),
                          QStringLiteral("920000001"), {});
                rows = shown(nobody);
                check(QStringLiteral("no messages and no punishment stays "
                                     "empty"),
                      rows.empty());

                // 5. other channel (shared chat) and a reused login
                auto shared = person(QStringLiteral("silent_user"),
                                     QStringLiteral("910000006"));
                {
                    MessageBuilder builder;
                    builder->flags.set(MessageFlag::System,
                                       MessageFlag::Timeout,
                                       MessageFlag::ModerationAction,
                                       MessageFlag::SharedMessage);
                    builder->channelName = QStringLiteral("other_room");
                    builder->timeoutUser = QStringLiteral("silent_user");
                    builder->timeoutUserID = QStringLiteral("910000006");
                    builder->messageText = QStringLiteral(
                        "silent_user has been banned in other_room.");
                    builder.emplace<TextElement>(builder->messageText,
                                                 MessageElementFlag::Text);
                    shared.messages->addMessage(builder.release(),
                                                MessageContext::Original);
                }
                rows = shown(shared);
                check(QStringLiteral("ban in another shared-chat channel is "
                                     "not shown"),
                      rows.empty());
                auto reused = person(QStringLiteral("silent_user"),
                                     QStringLiteral("910000006"));
                clearchat(reused.messages, QStringLiteral("silent_user"),
                          QStringLiteral("930000001"), {});
                rows = shown(reused);
                check(QStringLiteral("ban of another Twitch ID with the same "
                                     "login is not shown"),
                      rows.empty());

                // 7. switching people drops the previous card's notices
                rows = shown(silent);
                const auto silentRows = rows.size();
                rows = shown(muted);
                check(QStringLiteral("switching user shows only that user's "
                                     "punishment"),
                      silentRows == 1 && rows.size() == 1 &&
                          rows.front()->timeoutUser ==
                              QStringLiteral("muted_user"));

                // 8. "chat cleared by <moderator>" is not about the moderator
                auto moderator = person(QStringLiteral("mod_user"),
                                        QStringLiteral("910000009"));
                {
                    MessageBuilder builder;
                    builder->flags.set(MessageFlag::System,
                                       MessageFlag::ClearChat,
                                       MessageFlag::ModerationAction);
                    builder->timeoutUser = QStringLiteral("mod_user");
                    builder->messageText =
                        QStringLiteral("mod_user cleared the chat.");
                    builder.emplace<TextElement>(builder->messageText,
                                                 MessageElementFlag::Text);
                    moderator.messages->addMessage(builder.release(),
                                                   MessageContext::Original);
                }
                rows = shown(moderator);
                check(QStringLiteral("chat clear by a moderator is not shown "
                                     "as their punishment"),
                      rows.empty());
            }
#endif
            showCase(scenario);
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
            {
                bool storedOutsideProfile = false;
                for (auto *widget : popup.findChildren<QWidget *>())
                {
                    if (auto *label = dynamic_cast<Label *>(widget))
                    {
                        storedOutsideProfile |=
                            label->getText() ==
                                QStringLiteral("ID: ") + current.userId &&
                            !label->isVisible() &&
                            label->property("copy-text").toString() ==
                                current.userId;
                    }
                }
                check(QStringLiteral("UID remains stored and copyable without "
                                     "a technical profile row"),
                      storedOutsideProfile);
                auto *badges = popup.findChild<QWidget *>(
                    QStringLiteral("UsercardObservedBadges"));
                check(QStringLiteral("provided earned badge collection is "
                                     "visible (eight synthetic icons)"),
                      current.earnedBadges.size() == 8 && badges &&
                          badges->isVisible());
            }
#endif
            for (const auto &size : {QSize(960, 670), QSize(600, 760)})
            {
                popup.resize(size);
                auto *columns = popup.findChild<QSplitter *>(
                    QStringLiteral("UsercardColumns"));
                check(QStringLiteral("responsive %1x%2")
                          .arg(size.width())
                          .arg(size.height()),
                      columns && popup.width() <= size.width() &&
                          columns->orientation() ==
                              (popup.getLayoutContainer()->width() <
                                       int(520 * popup.scale())
                                   ? Qt::Vertical
                                   : Qt::Horizontal));
            }
            popup.resize(width, height);
            if (autoClose)
            {
                auto *tabs = popup.findChild<QTabWidget *>(
                    QStringLiteral("UsercardContent"));
                auto *profile = popup.findChild<QScrollArea *>(
                    QStringLiteral("UsercardProfileScroll"));
                check(
                    QStringLiteral(
                        "auto-close native containers retain mouse input"),
                    tabs && profile && tabs->hasMouseTracking() &&
                        profile->hasMouseTracking() &&
                        popup.windowFlags().testFlag(Qt::FramelessWindowHint));
                bool selected = false;
                auto *toggle = popup.findChild<QWidget *>(
                    QStringLiteral("UsercardArchiveToggle"));
                if (tabs && tabs->count() > 1 && toggle)
                {
                    const auto point = toggle->rect().center();
                    const auto global = toggle->mapToGlobal(point);
                    QMouseEvent press(QEvent::MouseButtonPress, QPointF(point),
                                      QPointF(global), Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease,
                                        QPointF(point), QPointF(global),
                                        Qt::LeftButton, Qt::NoButton,
                                        Qt::NoModifier);
                    QCoreApplication::sendEvent(toggle, &press);
                    QCoreApplication::sendEvent(toggle, &release);
                    selected = tabs->currentIndex() == 1;
                    tabs->setCurrentIndex(0);
                }
                check(QStringLiteral(
                          "header Archive link mouse events select Archive"),
                      selected);
            }
            check(QStringLiteral("no external network manager thread"),
                  NetworkManager::workerThread == nullptr);
        });
    }
    if (parser.isSet(QStringLiteral("native-hit-test-checks")))
    {
        QTimer::singleShot(500, &popup, [&] {
            const auto hits = nativeHitTests(popup);
            checksPassed &= !hits.isEmpty();
            for (auto it = hits.begin(); it != hits.end(); ++it)
            {
                const bool ok = it.value().toInt() == 1;  // HTCLIENT
                checksPassed &= ok;
                checks.append(
                    QJsonObject{{QStringLiteral("name"),
                                 QStringLiteral("native HTCLIENT ") + it.key()},
                                {QStringLiteral("ok"), ok},
                                {QStringLiteral("hit"), it.value()}});
            }
        });
    }
    const auto screenshot = parser.value(QStringLiteral("screenshot"));
    if (!screenshot.isEmpty())
    {
        if (parser.isSet(QStringLiteral("show-moderation")))
        {
            // after the synthetic profile data (which hides the strip) is set
            QTimer::singleShot(500, &popup, [&] {
                popup.previewModerationStrip();
            });
        }
        if (parser.isSet(QStringLiteral("banner-demo")))
        {
            QTimer::singleShot(300, &popup, [&] {
                if (auto *view = popup.findChild<ChannelView *>())
                {
                    auto *stack = new CornerBannerStack(view);
                    stack->setScale(popup.scale());
                    stack->show({.kind = CornerBannerData::Kind::GiftSubs,
                                 .displayName = QStringLiteral("doctorbrinen"),
                                 .amount = 5});
                    stack->show({.kind = CornerBannerData::Kind::Raid,
                                 .displayName = QStringLiteral("ZLOYn"),
                                 .amount = 1533});
                }
            });
        }
        if (parser.isSet(QStringLiteral("show-archive")))
        {
            QTimer::singleShot(450, &popup, [&] {
                if (auto *tabs = popup.findChild<QTabWidget *>(
                        QStringLiteral("UsercardContent")))
                {
                    tabs->setCurrentIndex(tabs->count() - 1);
                }
            });
        }
        QTimer::singleShot(650, &popup, [&, screenshot] {
            const bool saved = popup.grab().save(screenshot, "PNG");
            checksPassed &= saved;
            checks.append(
                QJsonObject{{QStringLiteral("name"),
                             QStringLiteral("own fixture widget rendered PNG")},
                            {QStringLiteral("ok"), saved}});
        });
    }
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
    if (historyChecks)
    {
        QTimer::singleShot(400, &popup, [&] {
            preview::runHistoryChecks(
                popup, split, *historyServer, fixture,
                [&](const QString &name, bool ok) {
                    checksPassed &= ok;
                    checks.append(QJsonObject{{QStringLiteral("name"), name},
                                              {QStringLiteral("ok"), ok}});
                },
                [&] {
                    if (!report.isEmpty())
                    {
                        checksPassed &=
                            writeReport(report, app, current, popup, checks);
                    }
                    qapp.exit(checksPassed ? 0 : 30);
                });
        });
        QTimer::singleShot(20000, &qapp, [&] {
            checksPassed = false;
            checks.append(
                QJsonObject{{QStringLiteral("name"),
                             QStringLiteral("history checks watchdog")},
                            {QStringLiteral("ok"), false}});
            if (!report.isEmpty())
            {
                writeReport(report, app, current, popup, checks);
            }
            qapp.exit(31);
        });
    }
#endif
    if (!report.isEmpty() && !historyChecks)
    {
        QTimer::singleShot(650, &popup, [&] {
            writeReport(report, app, current, popup, checks);
        });
    }
    if (parser.isSet(QStringLiteral("quit-after")) && !historyChecks)
    {
        QTimer::singleShot(
            std::clamp(parser.value(QStringLiteral("quit-after")).toInt(), 1000,
                       300000),
            &qapp, [&] {
                qapp.exit(checksPassed ? 0 : 30);
            });
    }
    std::ignore = popup.closing.connect([&controls, autoClose] {
        if (!autoClose)
        {
            controls.close();
        }
    });
    QElapsedTimer shutdownTimer;
    QObject::connect(&qapp, &QApplication::aboutToQuit, &qapp,
                     [&shutdownTimer] {
                         shutdownTimer.start();
                     });
    const int result = qapp.exec();
    std::fputs("offline preview: event loop exited\n", stderr);
    popupOwner.reset();
    std::fputs("offline preview: popup released\n", stderr);
    controlsOwner.reset();
    std::fputs("offline preview: controls released\n", stderr);
    splitOwner.reset();
    std::fputs("offline preview: split released\n", stderr);
    pendingServer.reset();
#ifndef LEAFYRINO_BASELINE_USERCARD_PREVIEW
    historyServer.reset();
#endif
    // The real app uses the same final deferred-delete drain. Keep the mock
    // controllers alive until all of the preview widgets have been released.
    for (int i = 0; i < 16; ++i)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    appOwner.reset();
    std::fprintf(
        stderr,
        "offline preview: controllers released; shutdown cleanup %lld ms\n",
        static_cast<long long>(shutdownTimer.elapsed()));
    return result;
}
