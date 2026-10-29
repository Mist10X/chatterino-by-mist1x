// SPDX-License-Identifier: MIT
// Offline native regression checks: actual Split, ChannelView rendering,
// hit-testing, mouse events, context menu and QTextEdit. No providers are started.
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/spellcheck/SpellChecker.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPersonalEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "singletons/CrashHandler.hpp"
#include "singletons/Resources.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitInput.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMouseEvent>
#include <QStyleFactory>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

using namespace chatterino;
namespace {

class OfflineApp final : public mock::BaseApplication
{
public:
    OfflineApp()
        : mock::BaseApplication(QStringLiteral("{}"))
        , windows(this->args, this->paths_, this->settings, this->theme,
                  this->fonts)
        , commands(this->paths_)
        , crash(this->paths_)
    {
        // Do not call account load(), provider initialize(), NetworkManager or
        // runGui(). BaseApplication redirects settings to its own QTemporaryDir.
        this->settings.disableSave();
        this->settings.showEmptyInput = true;
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
    SeventvPersonalEmotes *getSeventvPersonalEmotes() override
    {
        return &this->personalEmotes;
    }
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
    SeventvPersonalEmotes personalEmotes;
    SpellChecker spellChecker;
    CrashHandler crash;
};

class RecordingChannel final : public Channel
{
public:
    RecordingChannel() : Channel(QStringLiteral("offline-emote-rmb"), Type::Misc)
    {
    }
    void sendMessage(const QString &) override { ++this->sent; }
    int sent = 0;
};

QJsonArray checks;
int failures = 0;
QString progressPath;
void progress(const QString &stage)
{
    if (progressPath.isEmpty())
    {
        return;
    }
    QFile log(progressPath);
    if (log.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        log.write(QJsonDocument(QJsonObject{{"stage", stage}})
                      .toJson(QJsonDocument::Compact) + '\n');
        log.flush();
    }
}
void check(bool ok, const QString &name, const QString &actual = {})
{
    progress(name);
    checks.append(QJsonObject{{"name", name}, {"pass", ok}, {"actual", actual}});
    if (!ok)
    {
        ++failures;
    }
}

void pump()
{
    // Dispatch pending paint/layout events without adding a production delay.
    for (int i = 0; i != 4; ++i)
    {
        QApplication::processEvents();
    }
}

// Keep each distinct source pixmap alive: Image::fromResourcePixmap caches by
// its address. No URL, image download or emote-provider data is used.
std::vector<std::unique_ptr<QPixmap>> pixmaps;
EmotePtr emote(const QString &name, QColor color, bool zeroWidth = false,
               EmoteModifierPlacement placement = EmoteModifierPlacement::None)
{
    auto pixmap = std::make_unique<QPixmap>(28, 28);
    pixmap->fill(color);
    auto result = std::make_shared<Emote>(Emote{
        .name = EmoteName{name},
        .images = ImageSet{Image::fromResourcePixmap(*pixmap)},
        .tooltip = Tooltip{name},
        .homePage = {},
        .zeroWidth = zeroWidth,
        .id = {},
        .author = {},
        .baseName = {},
        .modifierPlacement = placement,
    });
    pixmaps.push_back(std::move(pixmap));
    return result;
}

std::optional<QPoint> visiblePixel(ChannelView &view, QColor color)
{
    pump();
    view.performLayout();
    const auto pixmap = view.grab();
    const auto image = pixmap.toImage();
    // Pick an interior pixel of an actual rendered emote. This goes through
    // normal production layout/hit-testing rather than a hardcoded coordinate.
    for (int y = 2; y + 2 < image.height(); ++y)
    {
        for (int x = 2; x + 2 < image.width(); ++x)
        {
            if (image.pixelColor(x, y).rgb() == color.rgb() &&
                image.pixelColor(x + 2, y + 2).rgb() == color.rgb())
            {
                return QPoint(qRound(x / pixmap.devicePixelRatio()),
                              qRound(y / pixmap.devicePixelRatio()));
            }
        }
    }
    return std::nullopt;
}

void rightClick(ChannelView &view, QPoint point,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    progress(QStringLiteral("rightClick constructing press"));
    const QPointF local(point);
    const QPointF global(view.mapToGlobal(point));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::RightButton,
                      Qt::RightButton, modifiers);
    progress(QStringLiteral("rightClick sending press"));
    QApplication::sendEvent(&view, &press);
    progress(QStringLiteral("rightClick press returned"));
    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                        Qt::RightButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(&view, &release);
    progress(QStringLiteral("rightClick release returned"));
    pump();
    progress(QStringLiteral("rightClick pump returned"));
}

void closeMenus()
{
    for (auto *widget : QApplication::allWidgets())
    {
        if (auto *menu = qobject_cast<QMenu *>(widget))
        {
            menu->close();
        }
    }
    pump();
}

bool visibleMenu()
{
    const auto widgets = QApplication::allWidgets();
    return std::ranges::any_of(widgets, [](auto *widget) {
        return qobject_cast<QMenu *>(widget) && widget->isVisible();
    });
}

void draft(Split &split, QTextEdit &edit, const QString &text,
           int position = -1, int selectionEnd = -1)
{
    progress(QStringLiteral("draft setting text"));
    split.getInput().setInputText(text);
    progress(QStringLiteral("draft text set"));
    auto cursor = edit.textCursor();
    cursor.setPosition(position < 0 ? text.size() : position);
    if (selectionEnd >= 0)
    {
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    }
    edit.setTextCursor(cursor);
    progress(QStringLiteral("draft cursor set"));
    pump();
    progress(QStringLiteral("draft pump returned"));
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication qt(argc, argv);
    qt.setApplicationName(QStringLiteral("Leafyrino-offline-emote-rmb-tests"));
    // The fork is forcibly portable, so Paths creates folders beside the EXE.
    // Refuse to run a copied fixture in any installed client/profile directory.
    if (!QDir::fromNativeSeparators(QCoreApplication::applicationFilePath())
             .startsWith(QStringLiteral("D:/devtools/"), Qt::CaseInsensitive) ||
        argc < 2)
    {
        return 20;
    }
    const auto reportPath = QFileInfo(QString::fromLocal8Bit(argv[1]));
    if (!reportPath.isAbsolute())
    {
        return 21;
    }
    progressPath = reportPath.absoluteFilePath() + QStringLiteral(".progress.jsonl");
    { QFile log(progressPath); log.open(QIODevice::WriteOnly | QIODevice::Truncate); }
    progress(QStringLiteral("QApplication created; guard passed"));
    qt.setStyle(QStyleFactory::create("Fusion"));
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
    qt.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    progress(QStringLiteral("initializing resources"));
    initResources();
    progress(QStringLiteral("resources initialized"));
    QElapsedTimer elapsed;
    elapsed.start();
    {
        progress(QStringLiteral("constructing offline application"));
        OfflineApp app;
        progress(QStringLiteral("offline application constructed"));
        QWidget host;
        QVBoxLayout layout(&host);
        progress(QStringLiteral("constructing split"));
        Split split(&host);
        progress(QStringLiteral("split constructed"));
        layout.addWidget(&split);
        host.resize(720, 380);
        auto channel = std::make_shared<RecordingChannel>();
        progress(QStringLiteral("setting synthetic channel"));
        split.setChannel(IndirectChannel(channel));
        progress(QStringLiteral("synthetic channel set"));
        host.show();
        progress(QStringLiteral("host shown"));
        pump();
        progress(QStringLiteral("initial events dispatched"));
        auto *edit = split.getInput().findChild<QTextEdit *>();
        check(edit != nullptr, QStringLiteral("real split input exists"));
        int menuCount = 0;
        auto connection = app.windows.channelViewContextMenuRequested.connect(
            [&](const ChannelView &, const auto &, const auto *, QMenu &) {
                ++menuCount;
            });

        const QColor marker(17, 234, 85);
        const auto smile = emote(QStringLiteral("RmbSmile"), marker);
        MessageBuilder builder;
        builder.emplace<EmoteElement>(smile, MessageElementFlag::Emote);
        channel->addMessage(builder.release(), MessageContext::Repost);
        auto &view = split.getChannelView();
        const auto point = visiblePixel(view, marker);
        check(point.has_value(), QStringLiteral("emote rendered and hit location found"));
        if (point && edit)
        {
            draft(split, *edit, {});
            rightClick(view, *point);
            check(edit->toPlainText() == "RmbSmile " && menuCount == 0 &&
                      !visibleMenu(),
                  QStringLiteral("RMB inserts into empty input without menu"),
                  edit->toPlainText());

            draft(split, *edit, QStringLiteral("draft"));
            rightClick(view, *point);
            check(edit->toPlainText() == "draft RmbSmile ",
                  QStringLiteral("draft preserved with separator at end"),
                  edit->toPlainText());

            draft(split, *edit, QStringLiteral("hello world"), 6);
            rightClick(view, *point);
            edit->insertPlainText(QStringLiteral("X"));
            check(edit->toPlainText() == "hello RmbSmile Xworld",
                  QStringLiteral("middle cursor and existing separator preserved"),
                  edit->toPlainText());

            draft(split, *edit, QStringLiteral("one replace two"), 4, 11);
            rightClick(view, *point);
            check(edit->toPlainText() == "one RmbSmile two",
                  QStringLiteral("selection replaced without losing surrounding draft"),
                  edit->toPlainText());

            draft(split, *edit, {});
            rightClick(view, *point);
            rightClick(view, *point);
            check(edit->toPlainText() == "RmbSmile RmbSmile ",
                  QStringLiteral("repeated RMB inserts separated codes"),
                  edit->toPlainText());

            draft(split, *edit, QStringLiteral("untouched"), 3);
            const auto beforeShiftMenus = menuCount;
            rightClick(view, *point, Qt::ShiftModifier);
            check(edit->toPlainText() == "untouched" &&
                      edit->textCursor().position() == 3 &&
                      menuCount == beforeShiftMenus + 1 && visibleMenu(),
                  QStringLiteral("Shift RMB keeps original menu and draft/cursor"));
            closeMenus();

            draft(split, *edit, QStringLiteral("readonly"));
            edit->setReadOnly(true);
            const auto beforeReadonlyMenus = menuCount;
            rightClick(view, *point);
            check(edit->toPlainText() == "readonly" &&
                      menuCount == beforeReadonlyMenus + 1,
                  QStringLiteral("read-only input is not changed"));
            closeMenus();
            edit->setReadOnly(false);

            edit->setEnabled(false);
            const auto beforeDisabledMenus = menuCount;
            rightClick(view, *point);
            check(edit->toPlainText() == "readonly" &&
                      menuCount == beforeDisabledMenus + 1,
                  QStringLiteral("disabled input is not changed"));
            closeMenus();
            edit->setEnabled(true);
        }

        // Use a new in-memory channel per shape, exercising normal setChannel.
        const auto show = [&](const std::function<void(MessageBuilder &)> &add) {
            auto next = std::make_shared<RecordingChannel>();
            MessageBuilder message;
            add(message);
            next->addMessage(message.release(), MessageContext::Repost);
            split.setChannel(IndirectChannel(next));
            pump();
            return next;
        };

        auto layeredChannel = show([&](MessageBuilder &message) {
            const auto overlay = emote(QStringLiteral("RmbOverlay"), marker, true);
            const auto modifier = emote(QStringLiteral("RmbModifier"), marker,
                                        false, EmoteModifierPlacement::Suffix);
            auto *layered = message.emplace<LayeredEmoteElement>(
                std::vector<LayeredEmoteElement::Emote>{
                    {smile, MessageElementFlag::Emote},
                    {overlay, MessageElementFlag::Emote}},
                MessageElementFlag::Emote);
            layered->addModifier(modifier);
        });
        const auto layeredPoint = visiblePixel(view, marker);
        check(layeredPoint.has_value(), QStringLiteral("layered emote rendered"));
        if (edit && layeredPoint)
        {
            draft(split, *edit, {});
            rightClick(view, *layeredPoint);
            check(edit->toPlainText() == "RmbSmile RmbOverlay RmbModifier ",
                  QStringLiteral("layered emote includes zero-width and modifier tokens"),
                  edit->toPlainText());
        }

        auto badgeChannel = show([&](MessageBuilder &message) {
            message.emplace<BadgeElement>(smile, MessageElementFlag::BadgeSubscription);
        });
        const auto badgePoint = visiblePixel(view, marker);
        check(badgePoint.has_value(), QStringLiteral("badge rendered"));
        if (edit && badgePoint)
        {
            draft(split, *edit, QStringLiteral("badge stays"));
            const auto before = menuCount;
            rightClick(view, *badgePoint);
            check(edit->toPlainText() == "badge stays" && menuCount == before + 1,
                  QStringLiteral("badge does not insert and keeps original menu"));
            closeMenus();
        }

        auto regexChannel = show([&](MessageBuilder &message) {
            message.emplace<EmoteElement>(
                emote(QStringLiteral("\\:-?\\)"), marker), MessageElementFlag::Emote);
        });
        const auto regexPoint = visiblePixel(view, marker);
        if (edit && regexPoint)
        {
            draft(split, *edit, {});
            rightClick(view, *regexPoint);
            check(edit->toPlainText() == ":) ",
                  QStringLiteral("Twitch regex emote becomes usable canonical code"),
                  edit->toPlainText());
        }
        else
        {
            check(false, QStringLiteral("Twitch regex emote rendered"));
        }

        ChannelView standalone(nullptr, ChannelView::Context::UserCard);
        standalone.resize(500, 180);
        standalone.setChannel(channel);
        standalone.show();
        const auto standalonePoint = visiblePixel(standalone, marker);
        check(standalonePoint.has_value(), QStringLiteral("standalone usercard emote rendered"));
        if (edit && standalonePoint)
        {
            draft(split, *edit, QStringLiteral("other input"));
            const auto before = menuCount;
            rightClick(standalone, *standalonePoint);
            check(edit->toPlainText() == "other input" && menuCount == before + 1,
                  QStringLiteral("no-Split usercard keeps menu and cannot change unrelated input"));
            closeMenus();
        }
        check(channel->sent == 0 && layeredChannel->sent == 0 &&
                  badgeChannel->sent == 0 && regexChannel->sent == 0,
              QStringLiteral("all clicks leave message send count at zero"));
        connection.disconnect();
        markAppAboutToQuit();
    }
    pixmaps.clear();
    QJsonObject report{{"checks", checks}, {"passed", checks.size() - failures},
                       {"failed", failures}, {"elapsed_ms", elapsed.elapsed()},
                       {"mode", "offscreen-real-widgets-no-network"}};
    QFile output(reportPath.absoluteFilePath());
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return 22;
    }
    output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    return failures == 0 ? 0 : 1;
}
