// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/MissedDigest.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/UiLanguage.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/splits/Split.hpp"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

#include <chrono>
#include <map>

namespace chatterino {

namespace {

/// Shorter absences are not worth a summary.
constexpr auto MIN_AWAY = std::chrono::minutes(5);

enum class Kind {
    Mention,
    Reply,
    Moderation,
    Raid,
    Announcement,
    Subscription,
    NewChatter,
};

QString label(Kind kind, int count)
{
    switch (kind)
    {
        case Kind::Mention:
            return uiText("mentions and highlights: %1").arg(count);
        case Kind::Reply:
            return uiText("replies to you: %1").arg(count);
        case Kind::Moderation:
            return uiText("bans and timeouts: %1").arg(count);
        case Kind::Raid:
            return uiText("raids: %1").arg(count);
        case Kind::Announcement:
            return uiText("announcements: %1").arg(count);
        case Kind::Subscription:
            return uiText("subs and gifts: %1").arg(count);
        case Kind::NewChatter:
            return uiText("new chatters: %1").arg(count);
    }
    return {};
}

class Digest final : public QObject
{
public:
    explicit Digest(Split *split)
        : QObject(split)
        , split_(split)
    {
        split->installEventFilter(this);
        QObject::connect(qApp, &QApplication::applicationStateChanged, this,
                         [this] {
                             this->update();
                         });
        // stand only: the card as if the user had been away for a while
        if (qEnvironmentVariable("MIST1X_DIGEST_TEST") == u"1")
        {
            QTimer::singleShot(6000, this, [this] {
                const auto snapshot =
                    this->split_->getChannel()->getMessageSnapshot();
                if (snapshot.size() > 40)
                {
                    this->lastSeen_ = snapshot[snapshot.size() - 40];
                    this->awayAt_ = this->lastSeen_->serverReceivedTime;
                    this->show(std::chrono::minutes(7));
                }
            });
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type())
        {
            case QEvent::Show:
            case QEvent::Hide:
            case QEvent::WindowActivate:
            case QEvent::WindowDeactivate:
            case QEvent::WindowStateChange:
                this->update();
                break;
            case QEvent::Resize:
                if (watched == this->card_.data() ||
                    watched == this->view())
                {
                    this->place();
                }
                break;
            default:
                break;
        }
        return false;
    }

private:
    ChannelView *view() const
    {
        return &this->split_->getChannelView();
    }

    bool present() const
    {
        auto *window = this->split_->window();
        return this->split_->isVisible() && window != nullptr &&
               window->isActiveWindow() && !window->isMinimized() &&
               QApplication::applicationState() == Qt::ApplicationActive;
    }

    void update()
    {
        // Show/Hide also come for the window being minimised and restored;
        // the application state covers switching to other programs
        if (auto *window = this->split_->window();
            window && !this->watchingWindow_)
        {
            window->installEventFilter(this);
            this->watchingWindow_ = true;
        }
        const bool here = this->present();
        if (!here && !this->away_)
        {
            this->away_ = true;
            this->awaySince_ = std::chrono::steady_clock::now();
            this->awayAt_ = QDateTime::currentDateTime();
            const auto channel = this->split_->getChannel();
            const auto last = channel->getMessageSnapshot(1);
            this->lastSeen_ = last.empty() ? nullptr : last.back();
            this->channel_ = channel;
        }
        else if (here && this->away_)
        {
            this->away_ = false;
            const auto away = std::chrono::steady_clock::now() - this->awaySince_;
            if (away >= MIN_AWAY && getSettings()->showMissedDigest &&
                this->split_->getChannel() == this->channel_.lock())
            {
                this->show(
                    std::chrono::duration_cast<std::chrono::minutes>(away));
            }
        }
    }

    void show(std::chrono::minutes away)
    {
        const auto channel = this->split_->getChannel();
        const auto snapshot = channel->getMessageSnapshot();
        size_t start = 0;
        if (this->lastSeen_)
        {
            for (size_t i = snapshot.size(); i-- > 0;)
            {
                if (snapshot[i] == this->lastSeen_)
                {
                    start = i + 1;
                    break;
                }
            }
        }
        const auto me = getApp()->getAccounts()->twitch.getCurrent();
        const auto myLogin = me->isAnon() ? QString() : me->getUserName();

        this->found_.clear();
        this->next_.clear();
        int chat = 0;
        for (size_t i = start; i < snapshot.size(); ++i)
        {
            const auto &message = snapshot[i];
            // only what came while away: the last message seen may be gone
            // from the chat (trimmed or replaced), and history loaded on a
            // reconnect is older
            if (message->serverReceivedTime.isValid() &&
                message->serverReceivedTime < this->awayAt_)
            {
                continue;
            }
            const auto flags = message->flags;
            const bool system = flags.has(MessageFlag::System);
            if (!system && !message->loginName.isEmpty())
            {
                ++chat;
            }
            if (!myLogin.isEmpty() && flags.has(MessageFlag::ReplyMessage) &&
                message->replyParent &&
                message->replyParent->loginName.compare(
                    myLogin, Qt::CaseInsensitive) == 0)
            {
                this->found_[Kind::Reply].push_back(message);
            }
            else if (!system &&
                     flags.hasAny({MessageFlag::ShowInMentions,
                                   MessageFlag::Highlighted}) &&
                     message->loginName.compare(myLogin, Qt::CaseInsensitive) !=
                         0)
            {
                this->found_[Kind::Mention].push_back(message);
            }
            if (flags.hasAny({MessageFlag::Timeout,
                              MessageFlag::ModerationAction,
                              MessageFlag::ClearChat}))
            {
                this->found_[Kind::Moderation].push_back(message);
            }
            if (message->messageText.contains(QStringLiteral("raider"),
                                              Qt::CaseInsensitive) &&
                (system || flags.has(MessageFlag::Subscription)))
            {
                this->found_[Kind::Raid].push_back(message);
            }
            else if (flags.has(MessageFlag::Subscription))
            {
                this->found_[Kind::Subscription].push_back(message);
            }
            if (flags.has(MessageFlag::Announcement))
            {
                this->found_[Kind::Announcement].push_back(message);
            }
            if (flags.has(MessageFlag::FirstMessage))
            {
                this->found_[Kind::NewChatter].push_back(message);
            }
        }
        if (this->found_.empty())
        {
            return;  // nothing worth a card, just chatter
        }

        QStringList links;
        for (const auto &[kind, messages] : this->found_)
        {
            links.append(QStringLiteral("<a href=\"%1\" style=\"color:%2; "
                                        "text-decoration:none\">%3</a>")
                             .arg(int(kind))
                             .arg(getTheme()->accent.name())
                             .arg(label(kind, int(messages.size()))));
        }
        const auto title =
            uiText("While you were away (%1 min, %2 messages):")
                .arg(away.count())
                .arg(chat);
        this->showCard(QStringLiteral("<b>%1</b><br>%2")
                           .arg(title.toHtmlEscaped(),
                                links.join(QStringLiteral(" · "))));
    }

    void showCard(const QString &html)
    {
        if (!this->card_)
        {
            auto *card = new QFrame(this->view());
            card->setObjectName("missedDigest");
            // clicks on the card must not start a text selection in the chat
            // under it
            card->setAttribute(Qt::WA_NoMousePropagation);
            card->setStyleSheet(
                QStringLiteral(
                    "QFrame#missedDigest { background: #221f2b; border: 1px "
                    "solid %1; border-left: 4px solid %1; border-radius: 8px; }"
                    "QLabel { color: #e6e3ee; background: transparent; "
                    "border: none; }"
                    "QPushButton { color: #9a96a6; background: transparent; "
                    "border: none; font-size: 15px; padding: 0 4px; }"
                    "QPushButton:hover { color: #ffffff; }")
                    .arg(getTheme()->accent.name()));
            auto *row = new QHBoxLayout(card);
            row->setContentsMargins(12, 8, 6, 8);
            auto *text = new QLabel;
            text->setWordWrap(true);
            text->setTextFormat(Qt::RichText);
            text->setObjectName("missedDigestText");
            QObject::connect(text, &QLabel::linkActivated, this,
                             [this](const QString &link) {
                                 this->jump(Kind(link.toInt()));
                             });
            row->addWidget(text, 1);
            auto *close = new QPushButton(QStringLiteral("✕"));
            close->setCursor(Qt::PointingHandCursor);
            close->setToolTip(uiText("Close"));
            QObject::connect(close, &QPushButton::clicked, card, &QWidget::hide);
            row->addWidget(close, 0, Qt::AlignTop);
            this->card_ = card;
            this->view()->installEventFilter(this);
        }
        auto *text = this->card_->findChild<QLabel *>("missedDigestText");
        text->setFont(getApp()->getFonts()->getFont(
            FontStyle::ChatMedium, getSettings()->getClampedUiScale()));
        text->setText(html);
        this->card_->show();
        this->place();
        this->card_->raise();
    }

    void place()
    {
        if (!this->card_)
        {
            return;
        }
        auto *view = this->view();
        const int width = std::max(160, view->width() - 24);
        auto *layout = this->card_->layout();
        const int height = layout->hasHeightForWidth()
                               ? layout->heightForWidth(width)
                               : this->card_->sizeHint().height();
        this->card_->setFixedSize(width, height);
        this->card_->move((view->width() - width) / 2, 8);
    }

    void jump(Kind kind)
    {
        auto it = this->found_.find(kind);
        if (it == this->found_.end() || it->second.empty())
        {
            return;
        }
        auto &index = this->next_[kind];
        const auto &message = it->second[index % it->second.size()];
        ++index;
        this->view()->scrollToMessage(message);
    }

    Split *split_;
    QPointer<QFrame> card_;
    bool away_ = false;
    bool watchingWindow_ = false;
    std::chrono::steady_clock::time_point awaySince_;
    QDateTime awayAt_;
    MessagePtr lastSeen_;
    std::weak_ptr<Channel> channel_;
    std::map<Kind, std::vector<MessagePtr>> found_;
    std::map<Kind, size_t> next_;
};

}  // namespace

void attachMissedDigest(Split *split)
{
    new Digest(split);
}

}  // namespace chatterino
