// SPDX-License-Identifier: MIT
// Actual IRC parser and chat rendering; synthetic data, no provider startup.
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "messages/Message.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "mocks/UserData.hpp"
#include "singletons/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "providers/twitch/IrcMessageHandler.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "singletons/Resources.hpp"
#include "singletons/WindowManager.hpp"
#include "util/VectorMessageSink.hpp"
#include "widgets/helper/ChannelView.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;
class OfflineApp : public mock::BaseApplication
{
public:
    OfflineApp()
        : mock::BaseApplication(QStringLiteral("{}"))
        , windows(this->args, this->paths_, this->settings, this->theme,
                  this->fonts)
    {
        this->settings.disableSave();
    }
    IUserDataController *getUserData() override
    {
        return &this->users;
    }
    SeventvPaints *getSeventvPaints() override { return &this->paints; }
    EmoteController *getEmotes() override { return &this->emotes; }
    SeventvPaints paints;
    mock::EmoteController emotes;
    WindowManager *getWindows() override
    {
        return &this->windows;
    }
    HotkeyController *getHotkeys() override
    {
        return &this->hotkeys;
    }
    AccountController *getAccounts() override
    {
        return &this->accounts;
    }
    ITwitchIrcServer *getTwitch() override { return &this->twitch; }
    PubSub *getTwitchPubSub() override { return &this->pubSub; }
    ILogging *getChatLogger() override { return &this->logging; }
    struct NoLog : ILogging {
        void addMessage(const QString &, MessagePtr, const QString &,
                        const QString &) override {}
        void closeChannel(const QString &, const QString &) override {}
    } logging;
    PubSub pubSub{QStringLiteral("wss://127.0.0.1:9")};
    mock::MockTwitchIrcServer twitch;
    WindowManager windows;
    HotkeyController hotkeys;
    AccountController accounts;
    mock::UserDataController users;
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication qt(argc, argv);
    if (argc != 2 || !QDir::fromNativeSeparators(qt.applicationFilePath())
                          .startsWith("D:/devtools/", Qt::CaseInsensitive))
    {
        return 20;
    }
    auto progress = [&](const QString &stage) {
        QFile log(QString::fromLocal8Bit(argv[1]) + ".progress");
        if (log.open(QIODevice::WriteOnly | QIODevice::Append))
            log.write(stage.toUtf8() + '\n');
    };
    progress("resources");
    initResources();
    progress("app");
    OfflineApp app;
    progress("channels");
    auto channel = std::make_shared<TwitchChannel>("offline-gifts");
    auto other = std::make_shared<TwitchChannel>("other-gifts");
    QJsonArray checks;
    int failures = 0;
    auto check = [&](bool ok, const QString &name) {
        progress(name + (ok ? " PASS" : " FAIL"));
        checks.append(QJsonObject{{"name", name}, {"pass", ok}});
        failures += !ok;
    };
    {
        // Settings connect() invokes its callback immediately, before start().
        StreamerMode mode;
        check(!mode.isEnabled(), "streamer-mode construction before worker start");
    }
    auto parse = [&](const QByteArray &tags, TwitchChannel *c = nullptr) {
        auto *irc = Communi::IrcMessage::fromData(
            "@login=alice;display-name=Alice;user-id=42;room-id=11;"
            "system-msg=Alice\\sgifted\\sa\\ssub\\sto\\sBob!;" +
                tags + " :tmi.twitch.tv USERNOTICE #offline-gifts",
            nullptr);
        VectorMessageSink sink;
        IrcMessageHandler::parseUserNoticeMessageInto(irc, sink,
                                                      c ? c : channel.get());
        delete irc;
        return sink.messages();
    };
    app.settings.hideCommunityGiftMessages = true;
    app.settings.showMassGiftBanner = true;
    app.settings.massGiftBannerMinimum = 5;
    auto small =
        parse("msg-id=submysterygift;id=small;msg-param-mass-gift-count=4");
    check(small.size() == 1 && small[0]->giftNotice,
          "below minimum: styled summary");
    const QByteArray recipient = "msg-id=subgift;msg-param-recipient-id=43;"
                                 "msg-param-recipient-user-name=bob;msg-param-"
                                 "recipient-display-name=Bob;";
    auto child = parse(recipient + "msg-param-community-gift-id=small");
    check(child.size() == 1 && child[0]->giftNotice,
          "small gift: recipient visible");
    check(parse("msg-id=submysterygift;id=large;msg-param-mass-gift-count=5")
              .empty(),
          "at minimum: summary replaced by banner");
    check(parse(recipient + "msg-param-community-gift-id=large").empty(),
          "large gift: no recipient flood");
    check(parse(recipient + "msg-param-community-gift-id=large", other.get())
                  .size() == 1,
          "parent state is channel-local");
    check(parse(recipient + "msg-param-community-gift-id=missing").size() == 1,
          "unknown parent: never lose notice");
    check(parse(recipient).size() == 1, "individual gift remains visible");
    auto anon =
        parse("msg-id=anonsubmysterygift;id=anon;msg-param-mass-gift-count=2");
    check(anon.size() == 1 && anon[0]->giftNotice,
          "small anonymous gift visible");
    app.settings.showMassGiftBanner = false;
    check(parse("msg-id=submysterygift;id=off;msg-param-mass-gift-count=50")
                  .size() == 1,
          "disabled banners: large summary visible");
    check(parse(recipient + "msg-param-community-gift-id=large").size() == 1,
          "disabled banners: recipients visible");
    app.settings.showMassGiftBanner = true;
    app.settings.hideCommunityGiftMessages = false;
    check(
        parse("msg-id=submysterygift;id=unhidden;msg-param-mass-gift-count=50")
                .size() == 1,
        "hide switch off: preserve ordinary notices");
    check(small[0]->clone()->giftNotice, "cloning preserves event style");
    for (int i = 0; i < 150; ++i)
    {
        parse("msg-id=submysterygift;msg-param-mass-gift-count=10;id=bounded-" +
              QByteArray::number(i));
    }
    check(channel->communityGiftBanners.size() <= 128, "parent cache bounded");

    // Same ChannelView as real chat, with the actual parsed notices.
    progress("preview channel");
    auto preview =
        std::make_shared<Channel>("gift-preview", Channel::Type::Misc);
    progress("adding preview messages");
    preview->addMessage(small[0], MessageContext::Original);
    preview->addMessage(child[0], MessageContext::Original);
    progress("constructing view");
    ChannelView view(nullptr, ChannelView::Context::UserCard);
    view.resize(700, 210);
    progress("setting preview channel");
    view.setChannel(preview);
    progress("showing view");
    view.show();
    qt.processEvents();
    progress("grabbing view");
    const auto rendered = view.grab();
    const auto background = rendered.toImage().pixelColor(650, 10);
    check(qAbs(background.red() - background.blue()) < 20,
          "gift notice has no full-width subscription highlight");
    check(rendered.save(QString::fromLocal8Bit(argv[1]) + ".png"),
          "real chat rendering saved");
    QFile report(QString::fromLocal8Bit(argv[1]));
    if (!report.open(QIODevice::WriteOnly))
        return 21;
    report.write(
        QJsonDocument(QJsonObject{{"checks", checks},
                                  {"failed", failures},
                                  {"passed", checks.size() - failures}})
            .toJson());
    markAppAboutToQuit();
    return failures ? 1 : 0;
}
