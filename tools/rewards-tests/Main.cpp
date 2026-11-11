// SPDX-License-Identifier: MIT
// Actual IRC parser and chat rendering; synthetic data, no provider startup.
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "messages/Message.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "providers/twitch/IrcMessageHandler.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Logging.hpp"
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
    SeventvPaints *getSeventvPaints() override
    {
        return &this->paints;
    }
    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }
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
    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }
    PubSub *getTwitchPubSub() override
    {
        return &this->pubSub;
    }
    ILogging *getChatLogger() override
    {
        return &this->logging;
    }
    struct NoLog : ILogging {
        void addMessage(const QString &, MessagePtr, const QString &,
                        const QString &) override
        {
        }
        void closeChannel(const QString &, const QString &) override
        {
        }
    } logging;
    PubSub pubSub{QStringLiteral("wss://127.0.0.1:9")};
    mock::MockTwitchIrcServer twitch;
    WindowManager windows;
    HotkeyController hotkeys;
    AccountController accounts;
    mock::UserDataController users;
};

#include "common/UiLanguage.hpp"
#include "widgets/dialogs/ChannelPointsDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
namespace chatterino {
class ChannelPointsDialogTestAccess
{
public:
    static void seed(ChannelPointsDialog &d,
                     const QVector<GqlChannelPointReward> &r)
    {
        d.initialFetchDone_ = true;
        d.rewards_ = r;
        d.rebuildContent();
    }
    static void detail(ChannelPointsDialog &d, const GqlChannelPointReward &r)
    {
        d.openRewardDetail(r);
    }
    static void back(ChannelPointsDialog &d)
    {
        d.showRewardsView();
    }
};
}  // namespace chatterino
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication qt(argc, argv);
    if (argc != 2)
        return 20;
    initResources();
    initializeUiLanguage("ru");
    OfflineApp app;
    auto channel = std::make_shared<TwitchChannel>("offline-rewards");
    channel->setChannelPointBalance(1000);
    ChannelPointsDialog dialog(channel.get(), nullptr);
    QVector<GqlChannelPointReward> rewards;
    for (int i = 1; i <= 8; ++i)
    {
        GqlChannelPointReward r;
        r.id = QString::number(i);
        r.title = QStringList{
            "Выбрать музыку",
            "Обнять чат",
            "Сообщение стримеру",
            "Показать кота",
            "Пить воду",
            "Заказать игру",
            "Приветствие",
            "Длинное название награды для проверки переноса"}[i - 1];
        r.prompt = i == 1 ? "любимая песня" : "Описание награды";
        r.cost = i * 250;
        r.isEnabled = true;
        r.isInStock = true;
        r.backgroundColor = "#a970ff";
        rewards.push_back(r);
    }
    auto hidden = rewards[0];
    hidden.id = "disabled";
    hidden.isEnabled = false;
    rewards.push_back(hidden);
    ChannelPointsDialogTestAccess::seed(dialog, rewards);
    dialog.resize(470, 650);
    dialog.show();
    qt.processEvents();
    auto cards = [&] {
        return dialog.findChildren<QPushButton *>(
            "ChannelPointsRewardCardButton");
    };
    QJsonArray checks;
    int failures = 0;
    auto check = [&](bool ok, QString name) {
        checks.append(QJsonObject{{"name", name}, {"pass", ok}});
        failures += !ok;
    };
    check(cards().size() == 8, "enabled supported rewards only");
    check(cards().front()->accessibleName().startsWith(rewards[0].title),
          "ascending price");
    check(!dialog.findChild<QLineEdit *>("ChannelPointsRewardSearch") &&
              !dialog.findChild<QComboBox *>("ChannelPointsRewardSort") &&
              !dialog.findChild<QCheckBox *>("ChannelPointsAffordableOnly"),
          "filter controls removed");
    channel->setChannelPointBalance(500);
    check(cards().size() == 8, "balance does not hide rewards");
    bool shortage = false;
    for (auto *c : cards())
        shortage |= c->toolTip().contains("Не хватает");
    check(shortage, "shortage tooltip");
    ChannelPointsDialogTestAccess::detail(dialog, rewards[0]);
    check(dialog.findChild<QPushButton *>("ChannelPointsUtilityButton")
              ->isHidden(),
          "refresh hidden in detail");
    ChannelPointsDialogTestAccess::back(dialog);
    check(cards().size() == 8, "back restores catalog");
    auto bits = rewards[0];
    bits.id = "bits";
    bits.title = "Gigantify";
    bits.isAutomatic = true;
    bits.rewardType = "SEND_GIGANTIFIED_EMOTE";
    bits.pricingType = "BITS";
    bits.cost = 1;
    app.settings.enableGigantifyEmotes = true;
    rewards.push_back(bits);
    ChannelPointsDialogTestAccess::seed(dialog, rewards);
    check(cards().back()->accessibleName().startsWith("Gigantify"),
          "Bits remain separate");
    channel->setChannelPointBalance(-1);
    check(cards().size() == 9, "unknown balance keeps all rewards");
    qt.processEvents();
    check(dialog.grab().save(QString::fromLocal8Bit(argv[1]) + ".png"),
          "catalog screenshot");
    dialog.resize(330, 510);
    qt.processEvents();
    check(dialog.grab().save(QString::fromLocal8Bit(argv[1]) + "-compact.png"),
          "compact screenshot");
    QFile out(QString::fromLocal8Bit(argv[1]));
    if (!out.open(QIODevice::WriteOnly))
        return 21;
    out.write(QJsonDocument(QJsonObject{{"checks", checks},
                                        {"failed", failures},
                                        {"passed", checks.size() - failures}})
                  .toJson());
    markAppAboutToQuit();
    return failures ? 1 : 0;
}
