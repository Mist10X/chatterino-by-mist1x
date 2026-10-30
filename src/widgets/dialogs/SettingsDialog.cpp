// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"

#include "Application.hpp"
#include "common/Args.hpp"
#include "common/QLogging.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/helper/SettingsDialogTab.hpp"
#include "widgets/settingspages/AboutPage.hpp"
#include "widgets/settingspages/AccountsPage.hpp"
#include "widgets/settingspages/CommandPage.hpp"
#include "widgets/settingspages/ExternalToolsPage.hpp"
#include "widgets/settingspages/FiltersPage.hpp"
#include "widgets/settingspages/GeneralPage.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/HighlightingPage.hpp"
#include "widgets/settingspages/IgnoresPage.hpp"
#include "widgets/settingspages/KeyboardSettingsPage.hpp"
#include "widgets/settingspages/LeafyrinoPage.hpp"
#include "widgets/settingspages/ModerationPage.hpp"
#include "widgets/settingspages/MoltorinoPage.hpp"
#include "widgets/settingspages/NicknamesPage.hpp"
#include "widgets/settingspages/NotificationPage.hpp"
#include "widgets/settingspages/PluginsPage.hpp"
#include "widgets/settingspages/TechnorinoPage.hpp"

#include <QDateTime>
#include <QDebug>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QLineEdit>
#include <QPointer>

using namespace Qt::Literals;

namespace chatterino {

namespace {

/// One topic of the settings: its sections in order. The settings themselves
/// come from the pages' code (see routeSetting()).
struct Topic {
    const char *name;
    const char *icon;
    std::vector<const char *> sections;
    SettingsTabId id = SettingsTabId::None;
};

const std::vector<Topic> &topics()
{
    static const std::vector<Topic> list{
        {"Appearance",
         ":/settings/topic-appearance.svg",
         {"Language and theme", "Window", "Tabs", "Chat header", "Messages",
          "Usernames", "Client detection", "Messages per second", "Overlay"},
         SettingsTabId::General},
        {"Chat",
         ":/settings/topic-chat.svg",
         {"Scrolling", "Mouse", "What to show in chat", "Hidden messages",
          "Unique chat (R9K)", "Mentions and whispers", "Reply threads",
          "Message history", "If I am banned or timed out", "Pinned Messages",
          "Poll and Prediction", "Points and Rewards", "Raids and gifted subs",
          "Chatter list", "Shared chat"},
         SettingsTabId::Leafyrino},
        {"Chat input",
         ":/settings/topic-input.svg",
         {"Input Box", "Slow mode", "Sending", "Autocompletion", "Commands"}},
        {"Emotes and badges",
         ":/settings/topic-emotes.svg",
         {"Emotes", "Emote providers", "Visible badges", "More badges",
          "7TV paints"}},
        {"User cards",
         ":/settings/topic-usercard.svg",
         {"Usercard", "Buttons and actions"}},
        {"Links and search",
         ":/settings/topic-links.svg",
         {"Link Previews", "Links", "Search"}},
        {"Translation", ":/settings/topic-translation.svg", {"Translation"}},
        {"Streamer Mode", ":/settings/topic-streamer.svg", {"Streamer Mode"}},
        {"Moderation tools", ":/settings/topic-moderation.svg", {"Moderation"}},
        {"Sign-in for extra features",
         ":/settings/topic-login.svg",
         {"Authentication"},
         SettingsTabId::Moltorino},
        {"YouTube and Kick", ":/settings/topic-platforms.svg", {"YouTube", "Kick"}},
        {"Fun", ":/settings/topic-fun.svg", {"Fun"}},
        {"System settings",
         ":/settings/topic-system.svg",
         {"Startup and tray", "Sound", "Connection", "Updates",
          "AppData & Cache", "Experimental"},
         SettingsTabId::Technorino},
    };
    return list;
}

class TopicPage : public SettingsPage
{
public:
    explicit TopicPage(const Topic &topic)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 0, 4, 0);
        this->view_ = GeneralPageView::withNavigation(this);
        layout->addWidget(this->view_);
        for (const auto *section : topic.sections)
        {
            this->view_->addTitle(QString::fromUtf8(section));
        }
    }

    GeneralPageView *view() const
    {
        return this->view_;
    }

    bool filterElements(const QString &query) override
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }

private:
    GeneralPageView *view_{};
};

QString settingsDialogStyle()
{
    const auto accent = getTheme()->accent;
    auto soft = accent;
    soft.setAlpha(60);
    return QStringLiteral(R"(
QLineEdit#settingsSearch {
    background: #1c1c24;
    border: 1px solid #2c2c37;
    border-radius: 9px;
    padding: 7px 12px;
    font-size: 14px;
    color: #ececf3;
    selection-background-color: %2;
}
QLineEdit#settingsSearch:focus {
    border: 1px solid %1;
}
QScrollArea#settingsSidebar, QScrollArea#settingsSidebar > QWidget,
QScrollArea#settingsSidebar > QWidget > QWidget {
    background: transparent;
}
QLabel#settingsCaption {
    color: #77768a;
    font-family: "Segoe UI Semibold";
    font-size: 11px;
    padding: 12px 0px 3px 20px;
}
chatterino--TitleLabel {
    font-family: "Segoe UI Semibold";
    font-size: 20px;
    color: #f2f1f8;
}
chatterino--SubtitleLabel {
    font-family: "Segoe UI Semibold";
    font-size: 15px;
    color: %1;
}
chatterino--NavigationLabel {
    font-family: "Segoe UI";
    font-size: 13px;
    color: #a3a1b2;
}
chatterino--SettingsPage #generalSettingsScrollContent {
    background: #19191f;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #34343f;
    border-radius: 3px;
    min-height: 32px;
}
QScrollBar::handle:vertical:hover {
    background: #45454f;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QPushButton {
    background: #25252e;
    border: 1px solid #363642;
    border-radius: 6px;
    padding: 4px 14px;
    color: #e6e6ee;
}
QPushButton:hover {
    background: #2f2f3a;
}
QPushButton:disabled {
    color: #74737f;
}
QDialogButtonBox QPushButton {
    background: #23232c;
    border: 1px solid #33333f;
    border-radius: 7px;
    padding: 6px 22px;
    color: #e6e6ee;
}
QDialogButtonBox QPushButton:hover {
    background: #2c2c37;
}
QDialogButtonBox QPushButton#settingsOk {
    background: %1;
    border: 1px solid %1;
    color: #15151b;
    font-family: "Segoe UI Semibold";
}
)")
        .arg(accent.name(), soft.name(QColor::HexArgb));
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : BaseWindow(
          {
              BaseWindow::Flags::DisableCustomScaling,
              BaseWindow::Flags::Dialog,
              BaseWindow::DisableLayoutSave,
              BaseWindow::BoundsCheckOnShow,
              BaseWindow::UseSettingsStylesheet,
          },
          parent)
{
    this->setObjectName("SettingsDialog");
    this->setWindowTitle(uiText("Leafyrino Settings"));
    this->setWindowRole(u"chatterino.settings"_s);
    // Disable the ? button in the titlebar until we decide to use it
    this->setWindowFlags(this->windowFlags() &
                         ~Qt::WindowContextHelpButtonHint);

    // Russian captions need more room; keep the established font and scale.
    this->resize(activeUiLanguage() == "ru" ? 1180 : 1020, 840);
    this->overrideBackgroundColor_ = QColor("#131318");
    this->setStyleSheet(this->styleSheet() + settingsDialogStyle());

    this->initUi();
    this->addTabs();

    this->addShortcuts();
    this->signalHolder_.managedConnect(getApp()->getHotkeys()->onItemsUpdated,
                                       [this]() {
                                           this->clearShortcuts();
                                           this->addShortcuts();
                                       });
}

void SettingsDialog::addShortcuts()
{
    this->setSearchPlaceholderText();
    HotkeyController::HotkeyMap actions{
        {"search",
         [this](std::vector<QString>) -> QString {
             this->ui_.search->setFocus();
             this->ui_.search->selectAll();
             return "";
         }},
        {"delete", nullptr},
        {"accept", nullptr},
        {"reject", nullptr},
        {"scrollPage", nullptr},
        {"openTab", nullptr},
    };

    this->shortcuts_ = getApp()->getHotkeys()->shortcutsForCategory(
        HotkeyCategory::PopupWindow, actions, this);
}
void SettingsDialog::setSearchPlaceholderText()
{
    QString searchHotkey;
    auto searchSeq = getApp()->getHotkeys()->getDisplaySequence(
        HotkeyCategory::PopupWindow, "search");
    if (!searchSeq.isEmpty())
    {
        searchHotkey =
            "(" + searchSeq.toString(QKeySequence::SequenceFormat::NativeText) +
            ")";
    }
    this->ui_.search->setPlaceholderText(uiText("Find in settings... ") + searchHotkey);
}

void SettingsDialog::initUi()
{
    auto outerBox = LayoutCreator<QWidget>(this->getLayoutContainer())
                        .setLayoutType<QVBoxLayout>()
                        .withoutSpacing();

    // TOP
    auto title = outerBox.emplace<PageHeader>();
    auto edit = LayoutCreator<PageHeader>(title.getElement())
                    .setLayoutType<QHBoxLayout>()
                    .withoutMargin()
                    .emplace<QLineEdit>()
                    .assign(&this->ui_.search);
    this->setSearchPlaceholderText();
    edit->setObjectName("settingsSearch");
    title->layout()->setContentsMargins(14, 12, 14, 0);
    edit->setClearButtonEnabled(true);
    edit->findChild<QAbstractButton *>()->setIcon(
        QPixmap(":/buttons/clearSearch.png"));
    this->ui_.search->installEventFilter(this);

    QObject::connect(edit.getElement(), &QLineEdit::textChanged, this,
                     &SettingsDialog::filterElements);

    // CENTER
    auto centerBox =
        outerBox.emplace<QHBoxLayout>().withoutMargin().withoutSpacing();

    // left side (tabs), scrolls when the window is low
    this->ui_.sidebar = new QScrollArea;
    this->ui_.sidebar->setObjectName("settingsSidebar");
    this->ui_.sidebar->setWidgetResizable(true);
    this->ui_.sidebar->setFrameShape(QFrame::NoFrame);
    this->ui_.sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->ui_.tabContainerContainer = new QWidget;
    this->ui_.tabContainer = new QVBoxLayout(this->ui_.tabContainerContainer);
    this->ui_.tabContainer->setContentsMargins(0, 0, 0, 0);
    this->ui_.sidebar->setWidget(this->ui_.tabContainerContainer);
    centerBox->addWidget(this->ui_.sidebar);
    this->updateSidebarWidth();

    // right side (pages)
    centerBox.emplace<QStackedLayout>()
        .assign(&this->ui_.pageStack)
        .withoutMargin();

    this->ui_.pageStack->setContentsMargins(0, 0, 0, 0);

    outerBox->addSpacing(12);

    // BOTTOM
    auto buttons = outerBox.emplace<QDialogButtonBox>(Qt::Horizontal);
    {
        this->ui_.okButton =
            buttons->addButton(uiText("Ok"), QDialogButtonBox::YesRole);
        this->ui_.okButton->setObjectName("settingsOk");
        this->ui_.cancelButton =
            buttons->addButton(uiText("Cancel"), QDialogButtonBox::NoRole);
    }

    buttons->setContentsMargins(0, 0, 12, 10);

    // ---- misc
    this->ui_.tabContainerContainer->setObjectName("tabWidget");
    this->ui_.pageStack->setObjectName("pages");

    QObject::connect(this->ui_.okButton, &QPushButton::clicked, this,
                     &SettingsDialog::onOkClicked);
    QObject::connect(this->ui_.cancelButton, &QPushButton::clicked, this,
                     &SettingsDialog::onCancelClicked);
}

void SettingsDialog::filterElements(const QString &text)
{
    // filter elements and hide pages
    for (auto &&tab : this->tabs_)
    {
        // filterElements returns true if anything on the page matches the search query
        tab->setVisible(tab->page()->filterElements(text) ||
                        tab->name().contains(text, Qt::CaseInsensitive));
    }

    // find next visible page
    if (this->lastSelectedByUser_ && this->lastSelectedByUser_->isVisible())
    {
        this->selectTab(this->lastSelectedByUser_, false);
    }
    else if (!this->selectedTab_->isVisible())
    {
        for (auto &&tab : this->tabs_)
        {
            if (tab->isVisible())
            {
                this->selectTab(tab, false);
                break;
            }
        }
    }

    // a caption shows while any tab under it does
    QWidget *caption = nullptr;
    bool captionHasTabs = false;
    const auto finishCaption = [&] {
        if (caption != nullptr)
        {
            caption->setVisible(captionHasTabs);
        }
    };
    for (int i = 0; i < this->ui_.tabContainer->count(); i++)
    {
        auto *widget = this->ui_.tabContainer->itemAt(i)->widget();
        if (widget == nullptr)
        {
            continue;
        }
        if (std::ranges::find(this->captions_, widget) != this->captions_.end())
        {
            finishCaption();
            caption = widget;
            captionHasTabs = false;
        }
        else
        {
            captionHasTabs |= !widget->isHidden();
        }
    }
    finishCaption();
}

void SettingsDialog::setElementFilter(const QString &query)
{
    this->ui_.search->setText(query);
}

bool SettingsDialog::eventFilter(QObject *object, QEvent *event)
{
    if (object == this->ui_.search && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = dynamic_cast<QKeyEvent *>(event);
        if (keyEvent == QKeySequence::DeleteStartOfWord &&
            this->ui_.search->selectionLength() > 0)
        {
            this->ui_.search->backspace();
            return true;
        }
    }
    return false;
}

void SettingsDialog::addTabs()
{
    this->ui_.tabContainer->setSpacing(1);
    this->ui_.tabContainer->setContentsMargins(0, 4, 0, 12);

    // The topic pages are built first, then the pages that hold the settings
    // send each of them to its topic and section (see routeSetting()).
    std::vector<std::pair<const Topic *, TopicPage *>> built;
    for (const auto &topic : topics())
    {
        // owned by the dialog from the start: a topic never opened must go
        // away with it (its settings point into the pages below)
        auto *page = new TopicPage(topic);
        page->setParent(this);
        page->hide();
        built.emplace_back(&topic, page);
    }
    SettingsTopics::setActive([&built](const QString &name) -> GeneralPageView * {
        for (const auto &[topic, page] : built)
        {
            if (name == QLatin1String(topic->name))
            {
                return page->view();
            }
        }
        return nullptr;
    });
    for (auto *source : std::initializer_list<SettingsPage *>{
             new GeneralPage, new LeafyrinoPage, new MoltorinoPage,
             new TechnorinoPage})
    {
        source->setParent(this);
        source->hide();
        this->sourcePages_.push_back(source);
    }
    SettingsTopics::setActive({});

    const auto addTopic = [&](const char *name) {
        for (const auto &[topic, page] : built)
        {
            if (qstrcmp(topic->name, name) == 0)
            {
                page->view()->finishTopic();
                this->addTab(
                    [page] {
                        return page;
                    },
                    uiText(QString::fromUtf8(topic->name)),
                    QString::fromUtf8(topic->icon), topic->id);
            }
        }
    };

    // Constructors are wrapped in std::function to remove some strain from first time loading.
    this->addCaption(uiText("Main settings"));
    addTopic("Appearance");
    addTopic("Chat");
    addTopic("Chat input");
    addTopic("Emotes and badges");
    addTopic("User cards");
    addTopic("Links and search");
    addTopic("Translation");
    addTopic("Streamer Mode");

    this->addCaption(uiText("Rules and moderation"));
    addTopic("Moderation tools");
    // clang-format off
    this->addTab([]{return new ModerationPage;},       uiText("Moderation buttons and logs"), ":/settings/topic-modbuttons.svg", SettingsTabId::Moderation);
    this->addTab([]{return new HighlightingPage;},     uiText("Highlights"),     ":/settings/topic-highlights.svg");
    this->addTab([]{return new IgnoresPage;},          uiText("Ignores"),        ":/settings/topic-ignores.svg");
    this->addTab([]{return new FiltersPage;},          uiText("Filters"),        ":/settings/topic-filters.svg");
    this->addTab([]{return new NicknamesPage;},        uiText("Nicknames"),      ":/settings/topic-nicknames.svg");
    this->addTab([]{return new CommandPage;},          uiText("Commands"),       ":/settings/topic-commands.svg");

    this->addCaption(uiText("Accounts and alerts"));
    this->addTab([]{return new AccountsPage;},         uiText("Accounts"),       ":/settings/topic-accounts.svg", SettingsTabId::Accounts);
    addTopic("Sign-in for extra features");
    this->addTab([]{return new NotificationPage;},     uiText("Live Notifications"),  ":/settings/topic-notifications.svg");

    this->addCaption(uiText("More settings"));
    addTopic("YouTube and Kick");
    addTopic("Fun");
    this->addTab([]{return new KeyboardSettingsPage;}, uiText("Hotkeys"),        ":/settings/topic-hotkeys.svg");
    this->addTab([]{return new ExternalToolsPage;},    uiText("External tools"), ":/settings/topic-tools.svg");
#ifdef CHATTERINO_HAVE_PLUGINS
    this->addTab([]{return new PluginsPage;},          uiText("Plugins"),        ":/settings/topic-plugins.svg");
#endif
    addTopic("System settings");
    this->ui_.tabContainer->addStretch(1);
    this->addTab([]{return new AboutPage;},            uiText("About"),          ":/settings/topic-about.svg", SettingsTabId::About, Qt::AlignBottom);
    // clang-format on
}

void SettingsDialog::addCaption(const QString &text)
{
    auto *label = new QLabel(text.toUpper());
    label->setObjectName("settingsCaption");
    this->ui_.tabContainer->addWidget(label);
    this->captions_.push_back(label);
}

void SettingsDialog::updateSidebarWidth()
{
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(14);
    const QFontMetrics metrics(font);
    int tabWidth = 190;
    for (const auto *entry : this->tabs_)
    {
        tabWidth = std::max(tabWidth,
                            metrics.horizontalAdvance(entry->name()) + 64);
    }
    this->ui_.tabContainerContainer->setFixedWidth(tabWidth);
    this->ui_.sidebar->setFixedWidth(tabWidth + 12);
}

void SettingsDialog::addTab(std::function<SettingsPage *()> page,
                            const QString &name, const QString &iconPath,
                            SettingsTabId id, Qt::Alignment alignment)
{
    auto *tab =
        new SettingsDialogTab(this, std::move(page), name, iconPath, id);
    tab->setFixedHeight(static_cast<int>(31 * this->dpi_));

    this->ui_.tabContainer->addWidget(tab, 0, alignment);
    this->tabs_.push_back(tab);
    this->updateSidebarWidth();

    if (this->tabs_.size() == 1)
    {
        this->selectTab(tab);
    }
}

void SettingsDialog::selectTab(SettingsDialogTab *tab, bool byUser)
{
    // add page if it's not been added yet
    [&] {
        for (int i = 0; i < this->ui_.pageStack->count(); i++)
        {
            if (this->ui_.pageStack->itemAt(i)->widget() == tab->page())
            {
                return;
            }
        }

        this->ui_.pageStack->addWidget(tab->page());
    }();

    this->ui_.pageStack->setCurrentWidget(tab->page());

    if (this->selectedTab_ != nullptr)
    {
        this->selectedTab_->setSelected(false);
    }

    tab->setSelected(true);
    this->selectedTab_ = tab;
    this->ui_.sidebar->ensureWidgetVisible(tab);
    if (byUser)
    {
        this->lastSelectedByUser_ = tab;
    }
}

void SettingsDialog::selectTab(SettingsTabId id)
{
    auto *t = this->tab(id);
    assert(t);
    if (!t)
    {
        return;
    }

    this->selectTab(t);
}

SettingsDialogTab *SettingsDialog::tab(SettingsTabId id)
{
    for (auto &&tab : this->tabs_)
    {
        if (tab->id() == id)
        {
            return tab;
        }
    }

    assert(false);
    return nullptr;
}

void SettingsDialog::showDialog(QWidget *parent,
                                SettingsDialogPreference preferredTab)
{
    static QPointer<SettingsDialog> instance;
    if (instance)
    {
        instance->refresh();
    }
    else
    {
        instance = new SettingsDialog(parent);
    }

    // Resets the cancel button.
    getSettings()->saveSnapshot();

    switch (preferredTab)
    {
        case SettingsDialogPreference::Accounts:
            instance->selectTab(SettingsTabId::Accounts);
            break;

        case SettingsDialogPreference::ModerationActions:
            if (auto *tab = instance->tab(SettingsTabId::Moderation))
            {
                instance->selectTab(tab);
                if (auto *page = dynamic_cast<ModerationPage *>(tab->page()))
                {
                    page->selectModerationActions();
                }
            }
            break;

        case SettingsDialogPreference::StreamerMode: {
            instance->selectTab(SettingsTabId::General);
        }
        break;

        case SettingsDialogPreference::About: {
            instance->selectTab(SettingsTabId::About);
        }
        break;

        case SettingsDialogPreference::Moltorino: {
            instance->selectTab(SettingsTabId::Moltorino);
        }
        break;

        default:;
    }

    if (instance->isMinimized())
    {
        instance->setWindowState(
            (instance->windowState() & ~Qt::WindowMinimized) |
            Qt::WindowActive);
    }
    if (instance->width() < 200 || instance->height() < 120)
    {
        instance->resize(1020, 840);
    }
    instance->show();
    if (preferredTab == SettingsDialogPreference::StreamerMode)
    {
        // this is needed because each time the settings are opened, the query is reset
        instance->setElementFilter("Streamer Mode");
    }
    instance->activateWindow();
    instance->raise();
    instance->setFocus();
}

void SettingsDialog::saveSnapshot(QWidget *parent, const QString &spec,
                                  const QString &file)
{
    showDialog(parent);
    SettingsDialog *dialog = nullptr;
    for (auto *widget : QApplication::allWidgets())
    {
        if (auto *d = dynamic_cast<SettingsDialog *>(widget))
        {
            dialog = d;
        }
    }
    if (dialog == nullptr)
    {
        return;
    }
    const auto name = spec.section(':', 0, 0);
    const auto scroll = spec.section(':', 1, 1).toInt();
    for (auto *tab : dialog->tabs_)
    {
        if (!name.isEmpty() && tab->name().contains(name, Qt::CaseInsensitive))
        {
            dialog->selectTab(tab);
            break;
        }
    }
    const auto query = spec.section(':', 2);
    if (!query.isEmpty())
    {
        dialog->setElementFilter(query);
    }
    QCoreApplication::processEvents();
    if (scroll > 0)
    {
        if (auto *page = dialog->ui_.pageStack->currentWidget())
        {
            for (auto *area : page->findChildren<QScrollArea *>())
            {
                area->verticalScrollBar()->setValue(scroll);
            }
        }
    }
    QCoreApplication::processEvents();
    dialog->grab().save(file, "PNG");
}

void SettingsDialog::refresh()
{
    // Update tabs that have already been opened. Calling page() here would
    // construct every settings page on repeated opens, which is rough during
    // startup and defeats the lazy loading above.
    for (auto *tab : this->tabs_)
    {
        if (auto *page = tab->createdPage())
        {
            page->onShow();
        }
    }
    for (auto *page : this->sourcePages_)
    {
        page->onShow();
    }
}

void SettingsDialog::scaleChangedEvent(float newScale)
{
    assert(newScale == 1.F &&
           "Scaling is disabled for the settings dialog - its scale should "
           "always be 1");

    for (SettingsDialogTab *tab : this->tabs_)
    {
        tab->setFixedHeight(31);
    }

    if (this->ui_.tabContainerContainer)
    {
        this->updateSidebarWidth();
    }
}

void SettingsDialog::showEvent(QShowEvent *e)
{
    this->ui_.search->setText("");
    BaseWindow::showEvent(e);
}

///// Widget creation helpers
void SettingsDialog::onOkClicked()
{
    if (!getApp()->getArgs().dontSaveSettings)
    {
        getApp()->getCommands()->save();
    }

    getSettings()->requestSave();

    this->close();
}

void SettingsDialog::onCancelClicked()
{
    getSettings()->restoreSnapshot();

    this->close();
}

}  // namespace chatterino
