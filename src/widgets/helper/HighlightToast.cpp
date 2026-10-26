// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/HighlightToast.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "messages/Message.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/Window.hpp"

#include <QEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>

#include <vector>

namespace chatterino {

namespace {

constexpr int LIFETIME_MS = 8000;
constexpr size_t MAX_CARDS = 3;

Split *findSplit(SplitContainer *page, const QString &channelName)
{
    if (page == nullptr)
    {
        return nullptr;
    }
    for (auto *split : page->getSplits())
    {
        if (split->getChannel()->getName().compare(channelName,
                                                   Qt::CaseInsensitive) == 0)
        {
            return split;
        }
    }
    return nullptr;
}

class Card;

/// Keeps the cards stacked at the top centre of the main window.
class Stack final : public QObject
{
public:
    explicit Stack(QWidget *window)
        : QObject(window)
        , window_(window)
    {
        window->installEventFilter(this);
    }

    void add(Card *card);
    void remove(Card *card);
    void relayout();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == this->window_ && event->type() == QEvent::Resize)
        {
            this->relayout();
        }
        return false;
    }

private:
    QWidget *window_;
    std::vector<QPointer<Card>> cards_;
};

class Card final : public QWidget
{
public:
    Card(QString channelName, MessagePtr message, Stack *stack,
         QWidget *parent)
        : QWidget(parent)
        , channelName_(std::move(channelName))
        , message_(std::move(message))
        , stack_(stack)
        , lifetime_(new QTimer(this))
    {
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setMouseTracking(true);
        this->setCursor(Qt::PointingHandCursor);
        this->setToolTip(this->message_->messageText);
        this->lifetime_->setSingleShot(true);
        QObject::connect(this->lifetime_, &QTimer::timeout, this, [this] {
            this->dismiss();
        });
        this->lifetime_->start(LIFETIME_MS);
    }

    float scale() const
    {
        return std::max(0.5F, getSettings()->getClampedUiScale());
    }

    QSize sizeHint() const override
    {
        const auto s = this->scale();
        const int available =
            this->parentWidget() ? this->parentWidget()->width() - int(24 * s)
                                 : int(440 * s);
        return {std::min(int(440 * s), std::max(int(220 * s), available)),
                int(58 * s)};
    }

    void dismiss()
    {
        this->stack_->remove(this);
        this->close();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        const auto s = this->scale();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool light = getTheme()->isLightTheme();
        painter.setPen(QPen(QColor(light ? "#b9a3e3" : "#5b4a82"), 1));
        painter.setBrush(QColor(light ? "#f4f0fb" : (this->hovered_ ? "#2a2535" : "#221f2b")));
        painter.drawRoundedRect(card, 8 * s, 8 * s);
        // accent strip on the left
        QPainterPath strip;
        strip.addRoundedRect(QRectF(card.left(), card.top(), 4 * s, card.height()),
                             2 * s, 2 * s);
        painter.fillPath(strip, QColor("#a884f2"));

        auto *fonts = getApp()->getFonts();
        const qreal pad = 12 * s;
        const qreal closeSize = 16 * s;
        const qreal textWidth = card.width() - 2 * pad - closeSize;
        const auto bold = fonts->getFont(FontStyle::ChatMediumBold, s);
        const auto normal = fonts->getFont(FontStyle::ChatMedium, s);
        const QFontMetricsF boldMetrics(bold);
        const QFontMetricsF normalMetrics(normal);

        // #channel · author
        const QString channel = QStringLiteral("#") + this->channelName_;
        const QString author = this->message_->displayName.isEmpty()
                                   ? this->message_->loginName
                                   : this->message_->displayName;
        qreal x = card.left() + pad;
        const qreal top = card.top() + 7 * s;
        painter.setFont(bold);
        painter.setPen(QColor(light ? "#684aa0" : "#c4abf5"));
        const auto channelText =
            boldMetrics.elidedText(channel, Qt::ElideRight, textWidth / 2);
        painter.drawText(QPointF(x, top + boldMetrics.ascent()), channelText);
        x += boldMetrics.horizontalAdvance(channelText + QStringLiteral("  "));
        auto color = this->message_->usernameColor;
        if (!color.isValid())
        {
            color = light ? QColor("#333") : QColor("#ddd");
        }
        painter.setPen(color);
        painter.drawText(QPointF(x, top + boldMetrics.ascent()),
                         boldMetrics.elidedText(author, Qt::ElideRight,
                                                card.left() + pad + textWidth - x));

        // the message
        painter.setFont(normal);
        painter.setPen(light ? QColor("#222") : QColor("#e6e3ee"));
        const qreal second = top + boldMetrics.height() + 3 * s;
        painter.drawText(QPointF(card.left() + pad, second + normalMetrics.ascent()),
                         normalMetrics.elidedText(
                             this->message_->messageText.simplified(),
                             Qt::ElideRight, textWidth));

        // close cross
        const QRectF cross(card.right() - pad - closeSize * 0.7, card.top() + 9 * s,
                           closeSize * 0.7, closeSize * 0.7);
        painter.setPen(QPen(light ? QColor("#777") : QColor("#9a96a6"), 1.4 * s));
        painter.drawLine(cross.topLeft(), cross.bottomRight());
        painter.drawLine(cross.topRight(), cross.bottomLeft());
        this->closeRect_ = cross.adjusted(-6 * s, -6 * s, 6 * s, 6 * s);
    }

    void enterEvent(QEnterEvent * /*event*/) override
    {
        this->hovered_ = true;
        this->lifetime_->stop();
        this->update();
    }

    void leaveEvent(QEvent * /*event*/) override
    {
        this->hovered_ = false;
        this->lifetime_->start(LIFETIME_MS / 2);
        this->update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }
        if (!this->closeRect_.contains(event->position()))
        {
            this->open();
        }
        this->dismiss();
    }

private:
    void open()
    {
        auto &notebook = getApp()->getWindows()->getMainWindow().getNotebook();
        for (int i = 0; i < notebook.getPageCount(); ++i)
        {
            auto *page = dynamic_cast<SplitContainer *>(notebook.getPageAt(i));
            if (auto *split = findSplit(page, this->channelName_))
            {
                notebook.select(page);
                split->setFocus(Qt::MouseFocusReason);
                split->getChannelView().scrollToMessage(this->message_);
                return;
            }
        }
    }

    QString channelName_;
    MessagePtr message_;
    Stack *stack_;
    QTimer *lifetime_;
    bool hovered_ = false;
    QRectF closeRect_;
};

void Stack::add(Card *card)
{
    this->cards_.insert(this->cards_.begin(), card);
    while (this->cards_.size() > MAX_CARDS)
    {
        if (auto old = this->cards_.back())
        {
            old->close();
        }
        this->cards_.pop_back();
    }
    card->show();
    card->raise();
    this->relayout();
}

void Stack::remove(Card *card)
{
    std::erase_if(this->cards_, [card](const QPointer<Card> &item) {
        return item.isNull() || item.data() == card;
    });
    this->relayout();
}

void Stack::relayout()
{
    std::erase_if(this->cards_, [](const QPointer<Card> &item) {
        return item.isNull();
    });
    qreal y = 0;
    for (const auto &card : this->cards_)
    {
        const auto s = card->scale();
        if (y == 0)
        {
            y = 36 * s;  // below the tab bar
        }
        const auto size = card->sizeHint();
        card->setGeometry((this->window_->width() - size.width()) / 2, int(y),
                          size.width(), size.height());
        card->raise();
        y += size.height() + 6 * s;
    }
}

QPointer<Stack> &stack()
{
    static QPointer<Stack> value;
    return value;
}

}  // namespace

void showHighlightToast(const QString &channelName, const MessagePtr &message)
{
    if (!message || channelName.isEmpty() || !getSettings()->showHighlightToasts)
    {
        return;
    }
    auto *app = tryGetApp();
    if (app == nullptr || app->getWindows() == nullptr)
    {
        return;
    }
    auto &window = app->getWindows()->getMainWindow();
    // Not when the message's channel is on the tab that is open now.
    auto *selected =
        dynamic_cast<SplitContainer *>(window.getNotebook().getSelectedPage());
    if (findSplit(selected, channelName) != nullptr)
    {
        return;
    }
    if (stack().isNull())
    {
        stack() = new Stack(&window);
    }
    stack()->add(new Card(channelName, message, stack().data(), &window));
}

void showHighlightToastOn(QWidget *window, const QString &channelName,
                          const MessagePtr &message)
{
    static QPointer<Stack> own;
    if (own.isNull() || own->parent() != window)
    {
        own = new Stack(window);
    }
    own->add(new Card(channelName, message, own.data(), window));
}

}  // namespace chatterino
