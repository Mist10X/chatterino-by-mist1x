// SPDX-License-Identifier: MIT
// Offline real-widget verification. Never loads accounts or starts providers.
#include "common/UiLanguage.hpp"
#include "common/ArchiveChatPolicy.hpp"
#include "messages/LimitedQueue.hpp"
#include "messages/MessageBuilder.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/Scrollbar.hpp"
#include "common/Version.hpp"
#include "controllers/tray/TrayController.hpp"
#include "widgets/Window.hpp"
#include "widgets/buttons/TitlebarButton.hpp"
#include <QLayout>
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/filters/lang/Tokenizer.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/spellcheck/SpellChecker.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "singletons/CrashHandler.hpp"
#include "singletons/Resources.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/dialogs/EditHotkeyDialog.hpp"
#include "widgets/dialogs/ChannelFilterEditorDialog.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/settingspages/CommandPage.hpp"
#include "widgets/settingspages/FiltersPage.hpp"
#include "widgets/settingspages/GeneralPage.hpp"
#include "widgets/settingspages/HighlightingPage.hpp"
#include "widgets/settingspages/IgnoresPage.hpp"
#include "widgets/settingspages/KeyboardSettingsPage.hpp"
#include "widgets/settingspages/LeafyrinoPage.hpp"
#include "widgets/settingspages/ModerationPage.hpp"
#include "widgets/settingspages/MoltorinoPage.hpp"
#include "widgets/settingspages/AccountsPage.hpp"
#include "widgets/settingspages/ExternalToolsPage.hpp"
#include "widgets/settingspages/AboutPage.hpp"
#include "widgets/settingspages/NicknamesPage.hpp"
#include "widgets/settingspages/TechnorinoPage.hpp"
#include "widgets/settingspages/UiComboBox.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QPainter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QLineEdit>
#include <QLocale>
#include <QScrollArea>
#include <QStyleFactory>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>

#include <functional>
#include <memory>

using namespace chatterino;
namespace {
class OfflineApp final : public mock::BaseApplication
{
public:
    explicit OfflineApp(bool archiveFontStartup = false)
        : mock::BaseApplication(archiveFontStartup
              ? QStringLiteral(R"({"appearance":{"currentFontFamily":"Archive Montserrat","currentFontSize":10,"currentFontWeight":600,"uiScale2":1.3}})")
              : QStringLiteral("{}"))
        , windows(this->args, this->paths_, this->settings, this->theme, this->fonts)
        , commands(this->paths_)
        , crash(this->paths_)
    {
        this->settings.disableSave();
        this->paths_.settingsDirectory = this->settingsDir.path();
    }
    WindowManager *getWindows() override { return &this->windows; }
    AccountController *getAccounts() override { return &this->accounts; }
    HotkeyController *getHotkeys() override { return &this->hotkeys; }
    CommandController *getCommands() override { return &this->commands; }
    EmoteController *getEmotes() override { return &this->emotes; }
    ITwitchIrcServer *getTwitch() override { return &this->twitch; }
    BttvEmotes *getBttvEmotes() override { return &this->bttv; }
    FfzEmotes *getFfzEmotes() override { return &this->ffz; }
    SeventvEmotes *getSeventvEmotes() override { return &this->seventv; }
    IUserDataController *getUserData() override { return &this->userData; }
    SeventvPaints *getSeventvPaints() override { return &this->paints; }
    SpellChecker *getSpellChecker() override { return &this->spellChecker; }
    CrashHandler *getCrashHandler() override { return &this->crash; }
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
    CrashHandler crash;
};
QJsonArray checks;
int failures = 0;
void check(bool value, const QString &name)
{
    checks.append(QJsonObject{{"name",name},{"pass",value}});
    if (!value)++failures;
}
QJsonArray texts(QWidget &root)
{
    QJsonArray result;
    for (auto *child : root.findChildren<QWidget *>())
    {
        QString text;
        if (auto *label = qobject_cast<QLabel *>(child))text=label->text();
        else if (auto *button = qobject_cast<QAbstractButton *>(child))text=button->text();
        else if (auto *edit=qobject_cast<QLineEdit *>(child))text=edit->placeholderText();
        if (!text.isEmpty())result.append(QJsonObject{{"class",child->metaObject()->className()},{"text",text},{"tooltip",child->toolTip()}});
        if (auto *combo=qobject_cast<QComboBox *>(child))
        {
            for(int i=0;i<combo->count();++i)
                result.append(QJsonObject{{"class","QComboBox"},{"text",combo->itemText(i)},{"source",combo->itemData(i,UiComboBox::SourceRole).toString()}});
        }
    }
    return result;
}
}

int main(int argc, char **argv)
{
    const auto exe = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    if (!exe.startsWith("D:/devtools/",Qt::CaseInsensitive))return 20;
    const bool fontStartup = argc > 2 && QByteArray(argv[2])=="--native-font-hidden";
    const bool captionOnly = fontStartup || (argc > 2 && QByteArray(argv[2])=="--native-caption-hidden");
    qputenv("QT_QPA_PLATFORM",captionOnly?"windows:fontengine=gdi":"offscreen");
    QApplication qt(argc,argv);
    qt.setApplicationName("Leafyrino-localization-offline-tests");
    qt.setStyle(QStyleFactory::create("Fusion"));
    // The offscreen QPA backend doesn't enumerate Windows' fonts. Seed only
    // this synthetic process; production font selection is unchanged.
    const auto fontId = QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    qt.setFont(QFont("Segoe UI", 9));
    check(fontId >= 0, "offscreen Windows font loaded");
    const auto output = argc > 1 ? QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1])) : QString();
    if (!output.startsWith("D:/devtools/",Qt::CaseInsensitive))return 21;
    QDir().mkpath(output);
    auto progress = [&](const QString &stage) {
        QFile log(output+"/ui-startup.jsonl");log.open(QIODevice::WriteOnly|QIODevice::Append);
        log.write(QJsonDocument(QJsonObject{{"stage",stage}}).toJson(QJsonDocument::Compact)+"\n");log.close();
    };
    progress("resources");
    initResources();
    qt.setWindowIcon(QIcon(":/icon.ico"));
    qt.setApplicationDisplayName("Chatterino by Mist1X");
    QFile stylesheet(":/qss/settings.qss");
    stylesheet.open(QIODevice::ReadOnly);
    qt.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    auto palette = qt.palette();
    palette.setColor(QPalette::Window, QColor("#161616"));
    palette.setColor(QPalette::Base, QColor("#333333"));
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Button, QColor("#464646"));
    palette.setColor(QPalette::ButtonText, Qt::white);
    qt.setPalette(palette);
    progress("mock application");
    OfflineApp app(fontStartup);
    progress("chat retention checks");
    const QByteArray retentionConfig("HT2\nrevision\ttest\nauto_trim\ton\nauto_trim_keep\t1000\ntrim_preserve_count\t2\ntrim_preserve\tmorphe_ya\ntrim_preserve\tdangerlyoha\nEND\n");
    auto retention = ArchiveChatPolicy::parse(retentionConfig);
    check(retention.has_value(), "retention complete Archive policy parsed");
    check(retention && retention->limitFor("MORPHE_YA", 500)==0 && retention->limitFor("other", 500)==0, "retention all chats grow between scheduled cleanups");
    auto disabledConfig = retentionConfig; disabledConfig.replace("auto_trim\ton", "auto_trim\toff");
    auto disabled = ArchiveChatPolicy::parse(disabledConfig);
    check(disabled && disabled->limitFor("morphe_ya", 500)==500, "retention disabled restores client setting");
    for (const auto &broken : {QByteArray("HT2\nauto_trim\ton\nEND\n"), retentionConfig.left(retentionConfig.size()-3), retentionConfig+"junk", QByteArray(150001, 'x')})
        check(!ArchiveChatPolicy::parse(broken), "retention rejects incomplete or oversized configuration");
    for (const auto &replacement : {QByteArray("0"), QByteArray("1000001"), QByteArray("-1"), QByteArray("bad")})
    {
        auto invalid = retentionConfig; invalid.replace("auto_trim_keep\t1000", "auto_trim_keep\t"+replacement);
        check(!ArchiveChatPolicy::parse(invalid), "retention rejects invalid count "+replacement);
    }
    auto mismatch=retentionConfig; mismatch.replace("trim_preserve_count\t2", "trim_preserve_count\t1");
    check(!ArchiveChatPolicy::parse(mismatch), "retention rejects incomplete exception list");
    auto empty=retentionConfig; empty.replace("trim_preserve_count\t2\ntrim_preserve\tmorphe_ya\ntrim_preserve\tdangerlyoha", "trim_preserve_count\t0");
    check(ArchiveChatPolicy::parse(empty)->preserved.empty(), "retention accepts explicitly empty exceptions");
    LimitedQueue<int> bounded(1000);
    for (int i=0;i<5010;++i) bounded.pushBack(i);
    check(bounded.size()==1000 && bounded.first()==4010 && bounded.last()==5009, "retention ordinary queue retains newest 1000");
    bounded.setLimit(0);
    for (int i=5010;i<15010;++i) bounded.pushBack(i);
    check(bounded.size()==11000 && bounded.first()==4010 && bounded.last()==15009, "retention preserved queue grows beyond old cap without dropping");
    bounded.pushFront(std::vector<int>{4008,4009});
    check(bounded.size()==11002 && bounded.first()==4008, "retention preserved queue prepends historical messages");
    bounded.insertBefore(4008,4007); bounded.insertAfter(15009,15010);
    check(bounded.first()==4007 && bounded.last()==15010, "retention unlimited queue insertion paths grow");
    bounded.setLimit(1000);
    check(bounded.size()==1000 && bounded.first()==14011 && bounded.last()==15010, "retention reduction preserves newest entries in order");
    LimitedQueue<int> beyondOldCeiling(0);
    for (int i=0;i<120005;++i) beyondOldCeiling.pushBack(i);
    check(beyondOldCeiling.size()==120005 && beyondOldCeiling.first()==0 && beyondOldCeiling.last()==120004,"retention exceeds actual previous 100000-message ceiling without loss");
    LimitedQueue<int> front(0); int remaining=3000;
    front.pushFrontWhile([&]() { return remaining ? remaining-- : 0; });
    check(front.size()==3000 && front.first()==1 && front.last()==3000, "retention preserved merge grows on demand");
    {
        auto channel=std::make_shared<Channel>("morphe_ya", Channel::Type::Twitch);
        pajlada::Signals::NoArgSignal changed;
        size_t limit=0;
        channel->followMessageLimit(changed, [&] { return limit; });
        ChannelView view(nullptr, ChannelView::Context::None, 1000);
        ChannelView second(nullptr, ChannelView::Context::None, 1000);
        view.setChannel(channel); second.setChannel(channel);
        for(int i=0;i<3200;++i)channel->addMessage(makeSystemMessage(QString::number(i)),MessageContext::Repost);
        check(channel->countMessages()==3200 && view.getMessagesSnapshot().size()==3200 && second.getMessagesSnapshot().size()==3200,"retention source, two views and proxy buffers preserve all 3200");
        check(view.getScrollBar().getHighlights().size()==3200,"retention scrollbar markers grow with preserved messages");
        limit=1000; changed.invoke();
        check(channel->countMessages()==1000 && view.getMessagesSnapshot().size()==1000 && second.getMessagesSnapshot().size()==1000,"retention live policy reduction updates all views");
        check(view.getScrollBar().getHighlights().size()==1000,"retention scrollbar shrinks with views");
        check(channel->getMessageSnapshot().front()->messageText=="2200","retention live reduction drops oldest only");
        limit=0; changed.invoke();
        for(int i=3200;i<4400;++i)channel->addMessage(makeSystemMessage(QString::number(i)),MessageContext::Repost);
        check(channel->countMessages()==2200 && view.getMessagesSnapshot().size()==2200,"retention adding exception takes effect without restart");
        view.setChannel(std::make_shared<Channel>("special",Channel::Type::Misc));
        check(view.getMessagesSnapshot().empty(),"retention switching away clears prior channel view");
        channel->clearMessages();
        check(second.getMessagesSnapshot().empty() && channel->countMessages()==0,"retention explicit chat clear remains functional");
        second.setChannel(std::make_shared<Channel>("other",Channel::Type::Misc));
        channel.reset(); changed.invoke();
        check(true,"retention destroyed channel disconnects policy callback");
    }
    {
        const auto originalPlugins = app.paths_.pluginsDirectory;
        app.paths_.pluginsDirectory = output + "/retention-reader-fixture";
        QDir().mkpath(app.paths_.pluginsDirectory + "/history-tools/data");
        const auto path=app.paths_.pluginsDirectory+"/history-tools/data/panel-control.txt";
        auto writePolicy=[&](const QByteArray &data) { QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"retention synthetic policy file opened");file.write(data); };
        auto poll=[&] { QEventLoop loop;QTimer::singleShot(2400,&loop,&QEventLoop::quit);loop.exec(); };
        writePolicy(retentionConfig);
        auto actual=std::make_shared<Channel>("ordinary",Channel::Type::Twitch);
        followArchiveChatPolicy(*actual);
        check(actual->messageLimit()==0,"retention real reader starts unbounded on ordinary channel");
        writePolicy(retentionConfig.left(24)); poll();
        check(actual->messageLimit()==0,"retention reader keeps last valid policy during partial write");
        writePolicy(disabledConfig); poll();
        check(actual->messageLimit()==size_t(app.settings.scrollbackSplitLimit.getValue()),"retention reader applies disabling from local file");
        writePolicy(retentionConfig); poll();
        check(actual->messageLimit()==0,"retention reader applies enabling without application restart");
        actual.reset();
        app.paths_.pluginsDirectory=originalPlugins;
    }
    progress("font state");
    const auto originalFont=qt.font();
    const auto originalChatFont=app.settings.chatFontFamily.getValue();
    const auto originalChatSize=app.settings.chatFontSize.getValue();
    const auto originalScale=app.settings.uiScale.getValue();
    const auto originalLocale=QLocale().name();
    const auto originalWeight=app.settings.chatFontWeight.getValue();
    if (fontStartup)
    {
        check(originalChatFont=="Mist Montserrat" && originalChatSize==10 && originalWeight==600,"font startup synthetic profile selected original family/size/weight");
        check(QFontInfo(app.fonts.getFont(FontStyle::ChatMedium,1.3f)).family()=="Mist Montserrat","font startup resolved bundled face before any settings change");
    }
    app.settings.chatFontFamily.setValue("Mist Montserrat");
    app.settings.chatFontSize.setValue(10);
    app.settings.chatFontWeight.setValue(600);
    qt.processEvents();
    check(QFontDatabase::families().contains("Mist Montserrat"),"selected missing font registered privately from resources");
    check(QFile::exists(":/licenses/montserrat-OFL.txt"),"bundled font OFL available");
    QJsonArray resolvedFonts;
    for (const auto style : {FontStyle::ChatMedium,FontStyle::ChatMediumBold,FontStyle::TimestampMedium})
    {
        const QFont font=app.fonts.getFont(style,1.3f);const QFontInfo actual(font);
        check(actual.family()=="Mist Montserrat","actual chat font resolves original family "+QString::number(int(style)));
        resolvedFonts.append(QJsonObject{{"style",int(style)},{"requested",font.family()},{"resolved",actual.family()},{"weight",actual.weight()},{"pointSize",actual.pointSizeF()}});
    }
    const auto chatFont=app.fonts.getFont(FontStyle::ChatMedium,1.3f);
    check(chatFont.weight()==600 && chatFont.pointSize()==13,"selected weight600 and scaled size13 retained");
    // The interface follows the chat font (one typeface for tabs, headers and chat).
    check(app.fonts.getFont(FontStyle::UiTabs,1).family()=="Mist Montserrat","UI font follows the chat font");
    check(app.settings.uiScale.getValue()==originalScale,"font repair does not change UI scale");
    QImage fontExample(950,165,QImage::Format_ARGB32_Premultiplied);fontExample.fill(QColor("#121216"));
    QPainter fontPainter(&fontExample);fontPainter.setFont(chatFont);fontPainter.setPen(QColor("#eeeaf4"));
    fontPainter.drawText(12,35,QString::fromUtf8("StreamElements: Заказывай и получай кешбэк 15%"));
    fontPainter.drawText(12,75,QString::fromUtf8("Grisha_Cement: Бери это архив"));
    fontPainter.drawText(12,115,QString::fromUtf8("dngminion: та их уже скупили"));fontPainter.end();
    check(fontExample.save(output+"/resolved-chat-font.png"),"actual Fonts chat font rendered without visible windows");
    QFile resolution(output+"/font-resolution.json");resolution.open(QIODevice::WriteOnly);resolution.write(QJsonDocument(QJsonObject{{"fonts",resolvedFonts},{"startup",fontStartup},{"uiFontFollowsChat",app.fonts.getFont(FontStyle::UiTabs,1).family()=="Mist Montserrat"}}).toJson());
    app.settings.chatFontFamily.setValue(originalChatFont);app.settings.chatFontSize.setValue(originalChatSize);app.settings.chatFontWeight.setValue(originalWeight);qt.processEvents();
    check(app.settings.chatFontFamily.getValue()==originalChatFont && app.settings.chatFontSize.getValue()==originalChatSize && app.settings.chatFontWeight.getValue()==originalWeight,"synthetic font settings restored");
    QJsonArray pages;
    std::vector<std::unique_ptr<Window>> brandingWindows;
    for (const auto &language : {QString("ru"),QString("en")})
    {
        progress("language "+language);
        initializeUiLanguage(language);
        check(Version::instance().fullVersion().startsWith("Chatterino by Mist1X "), language+" real version display branding");
        check(!Version::instance().fullVersion().contains("Nightly"),language+" display version omits Nightly");
        check(Version::instance().isNightly(),language+" actual nightly build metadata retained");
        check(uiText("Leafyrino")=="Chatterino by Mist1X", language+" localized display branding");
        check(uiText("Leafyrino Settings").contains("Chatterino by Mist1X"), language+" settings caption branding");
        check(qt.windowIcon().availableSizes().contains(QSize(256,256)), language+" embedded ICO contains 256px");
        check(qt.windowIcon().pixmap(QSize(256,256),1.0).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)==QImage(":/icon.png").convertToFormat(QImage::Format_ARGB32_Premultiplied), language+" ICO PNG rendered premultiplied artwork matches");
        for (const int size : {16,24,32,48,64,128,256})
        {
            const auto image=qt.windowIcon().pixmap(QSize(size,size),1.0).toImage();
            check(image.hasAlphaChannel() && image.pixelColor(0,0).alpha()==0 && image.pixelColor(image.width()-1,image.height()-1).alpha()==0,language+" icon transparent corners "+QString::number(size));
        }
        {
            auto mainOwner = std::make_unique<Window>(WindowType::Main, nullptr);
            auto &main = *mainOwner;
            main.setAttribute(Qt::WA_DontShowOnScreen, true);
            auto checkFrame = [&](const QString &phase) {
                if (!main.hasCustomWindowFrame())return;
                int captionButtons = 0;
                for (auto *widget : main.findChildren<QWidget *>())
                {
                    auto *button = dynamic_cast<TitleBarButton *>(widget);
                    if (!button)continue;
                    const auto style = button->getButtonStyle();
                    if (style != TitleBarButtonStyle::Minimize && style != TitleBarButtonStyle::Maximize && style != TitleBarButtonStyle::Unmaximize && style != TitleBarButtonStyle::Close)continue;
                    ++captionButtons;
                    check(button->minimumHeight()==int(30*main.scale()) && button->maximumHeight()==button->minimumHeight(),language+" caption button fixed height "+phase);
                    check(button->minimumWidth()>0 && button->maximumWidth()==button->minimumWidth(),language+" caption button fixed width "+phase);
                }
                check(captionButtons==3,language+" three caption buttons "+phase);
            };
            checkFrame("before native show");
            main.resize(900,600);main.show();qt.processEvents();
            auto checkHeader = [&](const QString &phase) {
                if (!main.hasCustomWindowFrame())return;
                const int height=main.layout()->itemAt(0)->geometry().height();
                QFile metrics(output+"/caption-metrics.jsonl");metrics.open(QIODevice::WriteOnly|QIODevice::Append);
                metrics.write(QJsonDocument(QJsonObject{{"language",language},{"phase",phase},{"height",height},{"scale",main.scale()},{"width",main.width()},{"windowHeight",main.height()}}).toJson(QJsonDocument::Compact)+"\n");
                check(height>0 && height<=qRound(32*main.scale()),language+" compact titlebar "+phase);
            };
            checkHeader("first content");
            checkFrame("after first layout");
            main.showMinimized();qt.processEvents();main.showNormal();qt.processEvents();
            checkHeader("minimize restore");checkFrame("minimize restore");
            main.resize(250,600);qt.processEvents();checkHeader("narrow");checkFrame("narrow");
            main.resize(900,600);qt.processEvents();checkHeader("wide again");checkFrame("wide again");
            main.hide();main.show();qt.processEvents();checkHeader("hide restore");
            for (const double scale : {1.5, 0.8})
            {
                app.settings.uiScale.setValue(scale);qt.processEvents();qt.processEvents();
                checkHeader("UI scale "+QString::number(scale));checkFrame("UI scale "+QString::number(scale));
            }
            app.settings.uiScale.setValue(originalScale);qt.processEvents();qt.processEvents();
            checkHeader("original UI scale restored");
            check(main.windowTitle()==Version::instance().fullVersion(),language+" actual main window caption");
            check(main.windowIcon().cacheKey()==qt.windowIcon().cacheKey(),language+" main window inherits application icon");
            check(main.grab().save(output+"/"+language+"-main.png"),language+" actual main window rendered");
            main.hide();
            brandingWindows.push_back(std::move(mainOwner));
        }
        if (captionOnly)continue; // no tray/dialog/provider work in native hidden mode
        {
            TrayController tray(app.windows);
            auto *icon = tray.findChild<QSystemTrayIcon *>();
            check(icon && icon->toolTip()=="Chatterino by Mist1X",language+" real tray tooltip");
            check(icon && icon->icon().pixmap(256).toImage()==qt.windowIcon().pixmap(256).toImage(),language+" tray artwork matches application icon");
            if (icon && icon->contextMenu())
            {
                const auto actions = icon->contextMenu()->actions();
                check(actions.first()->text().contains("Chatterino by Mist1X") && actions.last()->text().contains("Chatterino by Mist1X"),language+" real tray open and quit branding");
                icon->contextMenu()->setAttribute(Qt::WA_DontShowOnScreen,true);
                check(icon->contextMenu()->grab().save(output+"/"+language+"-tray-menu.png"),language+" real tray menu rendered");
            }
        }
        {
            QWidget gallery;
            gallery.setAttribute(Qt::WA_DontShowOnScreen,true);
            auto *row = new QHBoxLayout(&gallery);
            for (const int size : {16,24,32,48,64,128,256})
            {
                auto *column = new QVBoxLayout;
                auto *label = new QLabel;
                label->setPixmap(qt.windowIcon().pixmap(QSize(size,size),qt.devicePixelRatio()));
                column->addWidget(label,0,Qt::AlignCenter);
                column->addWidget(new QLabel(QString::number(size)),0,Qt::AlignCenter);
                row->addLayout(column);
            }
            gallery.adjustSize();
            for (const QString background : {QString("#101017"),QString("#ffffff")})
            {
                gallery.setAutoFillBackground(true);auto p=gallery.palette();p.setColor(QPalette::Window,QColor(background));gallery.setPalette(p);
                const QString suffix=background=="#101017"?"dark":"light";
                check(gallery.grab().save(output+"/"+language+"-icons-"+suffix+".png"),language+" transparent icons on "+suffix);
            }
            check(gallery.grab().save(output+"/"+language+"-icon-sizes.png"),language+" icon sizes rendered at actual DPR");
        }
        progress("basic checks "+language);
        check(uiText("Settings")==(language=="ru"?QString::fromUtf8("Настройки"):QString("Settings")),language+" settings");
        check(uiText(QString::fromUtf8("Архив"))==(language=="ru"?QString::fromUtf8("Архив"):QString("Archive")),language+" existing usercard strings");
        check(qt.font()==originalFont,language+" application font unchanged");
        check(QLocale().name()==originalLocale,language+" data locale unchanged");
        const auto nickname=QStringLiteral("General");
        const auto content=QStringLiteral("Save #Always Kappa Привет");
        QLabel userLabel(nickname);
        QLineEdit input(content);
        check(userLabel.text()==nickname && input.text()==content,language+" user data unchanged");
        check(uiText("User %1").arg(nickname).endsWith(nickname),language+" substituted nickname unchanged");
        check(uiText("https://example.test/Settings")=="https://example.test/Settings",language+" URL unchanged");
        UiComboBox options;
        options.addItem(uiText("Left"));options.setItemData(0,"Left",UiComboBox::SourceRole);
        options.addItem(uiText("Right"));options.setItemData(1,"Right",UiComboBox::SourceRole);
        options.setSourceText("Right");
        check(options.currentIndex()==1 && options.sourceText(1)=="Right",language+" raw option stable");
        if (language=="ru")check(options.currentText()==QString::fromUtf8("Справа"),"Russian option display");
        UiComboBox custom;
        custom.setEditable(true);
        custom.addItem(uiText("Left"));
        custom.setItemData(0,"Left",UiComboBox::SourceRole);
        custom.setCurrentIndex(-1);
        custom.setEditText("My custom value");
        check(custom.sourceText(-1)=="My custom value", language+" custom editable value unchanged");
        std::vector<std::pair<QString,std::function<QWidget *()>>> factories{
            {"settings",[]()->QWidget *{
                SettingsDialog::showDialog(nullptr);
                for (auto *window : QApplication::topLevelWidgets())
                    if (dynamic_cast<SettingsDialog *>(window))return window;
                return nullptr;
            }},
            {"general",[]{return new GeneralPage;}},
            {"leafyrino",[]{return new LeafyrinoPage;}},
            {"technorino",[]{return new TechnorinoPage;}},
            {"moltorino",[]{return new MoltorinoPage;}},
            {"accounts",[]{return new AccountsPage;}},
            {"external-tools",[]{return new ExternalToolsPage;}},
            {"about",[]{return new AboutPage;}},
            {"commands",[]{return new CommandPage;}},
            {"highlights",[]{return new HighlightingPage;}},
            {"ignores",[]{return new IgnoresPage;}},
            {"filters",[]{return new FiltersPage;}},
            {"hotkeys",[]{return new KeyboardSettingsPage;}},
            {"moderation",[]{return new ModerationPage;}},
            {"nicknames",[]{return new NicknamesPage;}},
            {"edit-hotkey",[]{return new EditHotkeyDialog(nullptr,nullptr);}},
            {"filter-editor",[]{return new ChannelFilterEditorDialog(nullptr);}},
        };
        // Settings pages schedule short layout timers that capture their views.
        // Keep pages alive while the fixture pumps those timers, as the real
        // SettingsDialog does; don't destroy a page between event-loop turns.
        std::vector<std::unique_ptr<QWidget>> ownedPages;
        for(const auto &[name,factory]:factories)
        {
            QFile progress(output+"/ui-progress.jsonl");
            progress.open(QIODevice::WriteOnly|QIODevice::Append);
            progress.write(QJsonDocument(QJsonObject{{"language",language},{"page",name}}).toJson(QJsonDocument::Compact)+"\n");
            progress.close();
            std::unique_ptr<QWidget> widget(factory());
            check(widget != nullptr, language+" "+name+" created");
            if (!widget)continue;
            if (name=="filter-editor")
            {
                check(static_cast<ChannelFilterEditorDialog *>(widget.get())->getFilter()==QStringLiteral("(message.content contains \"hello\")"),
                      language+" filter expression remains canonical");
                for (auto *combo : widget->findChildren<QComboBox *>())
                {
                    if (combo->findData("message text") < 0)continue;
                    for (int i = 0; i < combo->count(); ++i)
                    {
                        combo->setCurrentIndex(i);
                        const auto key = filters::VALID_IDENTIFIERS_MAP.key(combo->itemData(i).toString());
                        check(static_cast<ChannelFilterEditorDialog *>(widget.get())->getFilter() ==
                            QString("(%1 contains \"hello\")").arg(key),language+" canonical variable "+key);
                    }
                    break;
                }
            }
            widget->setAttribute(Qt::WA_DontShowOnScreen,true);
            widget->resize(name=="settings" ? widget->width() : 915,650);widget->show();
            qt.processEvents();
            if (name=="general")
            {
                auto *general=static_cast<GeneralPage *>(widget.get());
                check(general->filterElements(language=="ru"?QString::fromUtf8("шрифт"):QString("font")),language+" settings search");
                general->filterElements("");
                qt.processEvents();
                QComboBox *picker=nullptr;
                for(auto *combo:widget->findChildren<QComboBox *>())
                    if(combo->findData("ru")>=0 && combo->findData("en")>=0)picker=combo;
                check(picker!=nullptr,language+" language picker present");
                if(picker)
                {
                    app.settings.saveSnapshot();
                    picker->setCurrentIndex(picker->findData("en"));
                    check(app.settings.uiLanguage.getValue()=="en",language+" language code stored");
                    app.settings.restoreSnapshot();
                    picker->setCurrentIndex(picker->findData(language));
                    qt.processEvents();
                    QFile bounds(output+"/picker-bounds-"+language+".json");
                    bounds.open(QIODevice::WriteOnly);
                    const auto topLeft = picker->mapTo(widget.get(), QPoint(0,0));
                    bounds.write(QJsonDocument(QJsonObject{{"x",topLeft.x()},{"width",picker->width()},
                        {"root_width",widget->width()}}).toJson());
                }
            }
            if (name=="about")
            {
                auto *brand=widget->findChild<QLabel *>("BrandDisplayName");
                auto *attribution=widget->findChild<QLabel *>("BrandAttribution");
                check(brand && brand->text().contains("Chatterino by Mist1X"),language+" about brand heading");
                check(attribution && attribution->text().contains("Leafyrino") && attribution->text().contains("Chatterino7"),language+" upstream attribution preserved");
            }
            if(name=="settings" || name=="general" || name=="edit-hotkey" || name=="about")
                check(widget->grab().save(output+"/"+language+"-"+name+".png"),language+" "+name+" rendered");
            pages.append(QJsonObject{{"language",language},{"page",name},{"texts",texts(*widget)},{"width",widget->width()},{"height",widget->height()}});
            widget->hide();
            ownedPages.push_back(std::move(widget));
        }
        QEventLoop pendingLayouts;
        QTimer::singleShot(25, &pendingLayouts, &QEventLoop::quit);
        pendingLayouts.exec();
        ownedPages.clear();
    }
    check(app.settings.chatFontFamily.getValue()==originalChatFont && app.settings.chatFontSize.getValue()==originalChatSize && app.settings.uiScale.getValue()==originalScale,"persisted fonts and scale unchanged");
    initializeUiLanguage("unknown");
    check(activeUiLanguage()=="en" && uiText("Settings")=="Settings","unknown language falls back to English");
    QJsonObject result{{"checks",checks},{"failures",failures},{"pages",pages},{"font",originalFont.toString()},{"devicePixelRatio",qt.devicePixelRatio()},{"version",Version::instance().fullVersion()},{"scope","synthetic offline widgets only"}};
    QFile file(output+"/ui-results.json");file.open(QIODevice::WriteOnly);file.write(QJsonDocument(result).toJson());file.close();
    markAppAboutToQuit();
    return failures?1:0;
}
