// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/StreamStatsDialog.hpp"

#include "common/UiLanguage.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "providers/twitch/ChannelStats.hpp"
#include "singletons/Theme.hpp"

#include <QDialog>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace chatterino {

namespace {

const QColor BACKGROUND("#141419");
const QColor CARD("#1d1c25");
const QColor TEXT("#e8e6f0");
const QColor MUTED("#9a98a8");
const QColor FAINT("#5f5d6e");
/// for chatters without a color of their own
const QColor NO_COLOR("#b9b7c6");

constexpr int ROWS = 10;
constexpr int EMOTE_SIZE = 26;

QColor accent()
{
    return getTheme()->accent;
}

/// "18:02", or "29.10 18:02" for another day
QString timeText(const QDateTime &utcOrLocal)
{
    const auto time = utcOrLocal.toLocalTime();
    const QLocale locale;
    if (time.date() == QDate::currentDate())
    {
        return locale.toString(time, QStringLiteral("HH:mm"));
    }
    return locale.toString(time, QStringLiteral("dd.MM HH:mm"));
}

QString durationText(qint64 seconds)
{
    const auto minutes = std::max<qint64>(0, seconds / 60);
    if (minutes < 60)
    {
        return uiText("%1 min").arg(minutes);
    }
    return uiText("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

/// a chatter's color, lightened enough to read on the dark background
QColor readable(QColor color)
{
    if (!color.isValid())
    {
        return NO_COLOR;
    }
    auto hsl = color.toHsl();
    if (hsl.lightnessF() < 0.62F)
    {
        hsl.setHslF(hsl.hslHueF(), std::min(hsl.hslSaturationF(), 0.85F),
                    0.62F);
    }
    return hsl.toRgb();
}

QString number(qint64 value)
{
    return QLocale().toString(value);
}

QLabel *label(const QString &text, const QString &objectName)
{
    auto *widget = new QLabel(text);
    widget->setObjectName(objectName);
    return widget;
}

QWidget *card()
{
    auto *widget = new QWidget;
    widget->setObjectName(QStringLiteral("card"));
    widget->setAttribute(Qt::WA_StyledBackground);
    return widget;
}

/// one number with what it counts
QWidget *tile(const QString &value, const QString &caption,
              const QString &note = {})
{
    auto *widget = card();
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(2);
    layout->addWidget(label(value, QStringLiteral("tileValue")));
    layout->addWidget(label(caption, QStringLiteral("tileCaption")));
    if (!note.isEmpty())
    {
        layout->addWidget(label(note, QStringLiteral("tileNote")));
    }
    layout->addStretch(1);
    return widget;
}

/// A place in a ranking: a bar as long as its share of the first place, with
/// an optional picture, the name and the number.
class RankRow : public QWidget
{
public:
    RankRow(int place, QString name, QColor color, int count, double share,
            QPixmap picture = {})
        : place_(place)
        , name_(std::move(name))
        , color_(std::move(color))
        , count_(count)
        , share_(share)
        , picture_(std::move(picture))
    {
        this->setFixedHeight(EMOTE_SIZE + 8);
        this->setToolTip(QStringLiteral("%1 — %2").arg(this->name_,
                                                         number(count)));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area = this->rect().adjusted(0, 1, 0, -1);

        QPainterPath background;
        background.addRoundedRect(area, 7, 7);
        painter.fillPath(background, CARD);
        auto bar = accent();
        bar.setAlphaF(0.22);
        QPainterPath filled;
        filled.addRoundedRect(
            QRectF(area.left(), area.top(),
                   std::max(14.0, area.width() * this->share_), area.height()),
            7, 7);
        painter.fillPath(filled, bar);

        int x = 10;
        painter.setPen(FAINT);
        painter.drawText(QRect(x, 0, 22, this->height()), Qt::AlignVCenter,
                         QString::number(this->place_));
        x += 22;
        if (this->hasPictureSlot())
        {
            if (!this->picture_.isNull())
            {
                auto scaled = this->picture_.scaled(
                    EMOTE_SIZE * 2, EMOTE_SIZE, Qt::KeepAspectRatio,
                    Qt::SmoothTransformation);
                painter.drawPixmap(
                    x, (this->height() - scaled.height()) / 2, scaled);
                x += scaled.width() + 8;
            }
            else
            {
                painter.fillRect(QRect(x, (this->height() - EMOTE_SIZE) / 2,
                                       EMOTE_SIZE, EMOTE_SIZE),
                                 QColor("#2a2935"));
                x += EMOTE_SIZE + 8;
            }
        }

        const auto countText = number(this->count_);
        const auto countWidth =
            painter.fontMetrics().horizontalAdvance(countText);
        painter.setPen(MUTED);
        painter.drawText(QRect(this->width() - countWidth - 10, 0,
                               countWidth, this->height()),
                         Qt::AlignVCenter, countText);

        auto font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(this->color_.isValid() ? this->color_ : NO_COLOR);
        const auto nameWidth = this->width() - x - countWidth - 22;
        painter.drawText(
            QRect(x, 0, nameWidth, this->height()), Qt::AlignVCenter,
            painter.fontMetrics().elidedText(this->name_, Qt::ElideRight,
                                             nameWidth));
    }

private:
    bool hasPictureSlot() const
    {
        return !this->picture_.isNull() || !this->color_.isValid();
    }

    int place_;
    QString name_;
    QColor color_;
    int count_;
    double share_;
    QPixmap picture_;
};

/// Messages over time, in bars of 1–60 minutes; pointing at a bar tells its
/// time and count.
class ActivityChart : public QWidget
{
public:
    ActivityChart(const QMap<qint64, int> &perMinute, qint64 fromMinute,
                  qint64 toMinute)
    {
        this->setMouseTracking(true);
        this->setMinimumHeight(140);
        const auto span = std::max<qint64>(1, toMinute - fromMinute + 1);
        for (int step : {1, 2, 5, 10, 15, 30, 60})
        {
            this->step_ = step;
            if (span / step <= 90)
            {
                break;
            }
        }
        this->from_ = fromMinute - fromMinute % this->step_;
        const auto count = (toMinute - this->from_) / this->step_ + 1;
        this->bars_.fill(0, qsizetype(std::max<qint64>(1, count)));
        for (auto it = perMinute.cbegin(); it != perMinute.cend(); ++it)
        {
            const auto index = (it.key() - this->from_) / this->step_;
            if (index >= 0 && index < this->bars_.size())
            {
                this->bars_[index] += it.value();
            }
        }
        this->max_ = std::max(1, *std::max_element(this->bars_.cbegin(),
                                                    this->bars_.cend()));
    }

protected:
    QRectF plot() const
    {
        return QRectF(12, 22, this->width() - 24, this->height() - 46);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath background;
        background.addRoundedRect(this->rect(), 10, 10);
        painter.fillPath(background, CARD);

        const auto area = this->plot();
        painter.setPen(FAINT);
        painter.drawText(QRectF(area.left(), 4, area.width(), 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         uiText("up to %1 messages per %2 min")
                             .arg(number(this->max_))
                             .arg(this->step_));

        const auto width = area.width() / double(this->bars_.size());
        const auto gap = width > 4 ? 1.5 : 0.5;
        for (qsizetype i = 0; i < this->bars_.size(); ++i)
        {
            const auto value = this->bars_[i];
            if (value == 0)
            {
                continue;
            }
            const auto height =
                std::max(2.0, area.height() * value / double(this->max_));
            auto color = accent();
            if (i != this->hovered_)
            {
                color.setAlphaF(0.75);
            }
            QPainterPath bar;
            bar.addRoundedRect(QRectF(area.left() + i * width + gap / 2,
                                      area.bottom() - height,
                                      std::max(1.0, width - gap), height),
                               2, 2);
            painter.fillPath(bar, color);
        }
        painter.setPen(QColor("#2f2d3a"));
        painter.drawLine(area.bottomLeft(), area.bottomRight());

        // times under the bars: the start, the middle and the end
        painter.setPen(MUTED);
        const auto labelTop = area.bottom() + 4;
        const auto at = [this](qsizetype index) {
            return timeText(QDateTime::fromSecsSinceEpoch(
                (this->from_ + index * this->step_) * 60));
        };
        const auto last = this->bars_.size() - 1;
        painter.drawText(QRectF(area.left(), labelTop, 120, 16),
                         Qt::AlignLeft, at(0));
        if (last > 4)
        {
            painter.drawText(
                QRectF(area.center().x() - 60, labelTop, 120, 16),
                Qt::AlignHCenter, at(last / 2));
        }
        painter.drawText(QRectF(area.right() - 120, labelTop, 120, 16),
                         Qt::AlignRight, at(last));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const auto area = this->plot();
        const auto width = area.width() / double(this->bars_.size());
        const auto index = qsizetype(
            std::floor((event->position().x() - area.left()) / width));
        const auto hovered =
            index >= 0 && index < this->bars_.size() ? int(index) : -1;
        if (hovered != this->hovered_)
        {
            this->hovered_ = hovered;
            this->update();
        }
        if (hovered < 0)
        {
            QToolTip::hideText();
            return;
        }
        const auto start =
            QDateTime::fromSecsSinceEpoch((this->from_ + hovered * this->step_) * 60);
        const auto text =
            this->step_ == 1
                ? uiText("%1: %2 messages")
                      .arg(timeText(start), number(this->bars_[hovered]))
                : uiText("%1–%2: %3 messages")
                      .arg(timeText(start),
                           QLocale().toString(start.addSecs(this->step_ * 60),
                                              QStringLiteral("HH:mm")),
                           number(this->bars_[hovered]));
        QToolTip::showText(event->globalPosition().toPoint(), text, this);
    }

    void leaveEvent(QEvent *) override
    {
        this->hovered_ = -1;
        this->update();
    }

private:
    QList<int> bars_;
    qint64 from_ = 0;
    int step_ = 1;
    int max_ = 1;
    int hovered_ = -1;
};

template <typename T, typename Count>
QList<std::pair<QString, T>> top(const QHash<QString, T> &all, Count count)
{
    QList<std::pair<QString, T>> list;
    list.reserve(all.size());
    for (auto it = all.cbegin(); it != all.cend(); ++it)
    {
        list.append({it.key(), it.value()});
    }
    std::sort(list.begin(), list.end(), [&](const auto &a, const auto &b) {
        const auto left = count(a.second);
        const auto right = count(b.second);
        return left != right ? left > right : a.first < b.first;
    });
    if (list.size() > ROWS)
    {
        list.resize(ROWS);
    }
    return list;
}

QWidget *section(const QString &title, QWidget *body)
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(label(title, QStringLiteral("section")));
    layout->addWidget(body);
    return widget;
}

/// Everything the window shows; `waiting` is set when a picture still loads.
QWidget *build(const QString &channelName, bool &waiting)
{
    const auto data = ChannelStats::get(channelName);
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("content"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(22, 18, 22, 22);
    layout->setSpacing(16);

    // the channel and the stream
    {
        auto *head = new QHBoxLayout;
        head->setSpacing(10);
        head->addWidget(label(channelName, QStringLiteral("channel")));
        if (data.live)
        {
            head->addWidget(label(uiText("LIVE"), QStringLiteral("live")));
        }
        head->addStretch(1);
        layout->addLayout(head);

        QString line;
        const auto now = QDateTime::currentDateTime();
        if (!data.since.isValid())
        {
            line = uiText("This channel is not open, so nothing is counted.");
        }
        else if (data.live)
        {
            line = uiText("The stream started at %1 and has been going for %2")
                       .arg(timeText(data.since),
                            durationText(data.since.secsTo(now)));
        }
        else if (data.sinceStreamStart)
        {
            line = uiText("The stream that started at %1 has ended")
                       .arg(timeText(data.since));
        }
        else
        {
            line = uiText("The channel is offline: counting since %1, when the "
                          "chat was opened")
                       .arg(timeText(data.since));
        }
        auto *sub = label(line, QStringLiteral("subtitle"));
        sub->setWordWrap(true);
        layout->addWidget(sub);
        if (!data.since.isValid())
        {
            layout->addStretch(1);
            return content;
        }
    }

    // the beginning of the stream, read from the public logs
    using History = ChannelStatsData::History;
    if (data.history != History::NotNeeded && data.history != History::Done)
    {
        auto *box = card();
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(14, 10, 14, 12);
        boxLayout->setSpacing(8);
        QString text;
        if (data.history == History::Loading)
        {
            const auto total = std::max<qint64>(
                1, data.since.secsTo(data.historyUntil));
            const auto done = data.since.secsTo(data.historyRead);
            const int percent = int(std::clamp<qint64>(done * 100 / total, 0, 99));
            text = uiText("Loading the beginning of the stream (before %1) from "
                          "the public chat logs… %2%")
                       .arg(timeText(data.historyUntil))
                       .arg(percent);
            auto *progress = new QProgressBar;
            progress->setRange(0, 100);
            progress->setValue(percent);
            progress->setTextVisible(false);
            progress->setFixedHeight(4);
            auto *note = label(text, QStringLiteral("note"));
            note->setWordWrap(true);
            boxLayout->addWidget(note);
            boxLayout->addWidget(progress);
        }
        else
        {
            text = data.history == History::Partial
                       ? uiText("Some of the stream before %1 is missing from "
                                "the public chat logs, so the numbers may be a "
                                "little low.")
                       : uiText("This channel is not in the public chat logs: "
                                "the numbers start at %1, when the chat was "
                                "opened.");
            auto *note = label(text.arg(timeText(data.historyUntil)),
                               QStringLiteral("note"));
            note->setWordWrap(true);
            boxLayout->addWidget(note);
        }
        layout->addWidget(box);
    }

    // the numbers
    {
        const auto end = data.live || data.perMinute.isEmpty()
                             ? QDateTime::currentDateTime()
                             : QDateTime::fromSecsSinceEpoch(
                                   (data.perMinute.lastKey() + 1) * 60);
        const auto minutes = std::max<qint64>(1, data.since.secsTo(end) / 60);
        int peak = 0;
        qint64 peakMinute = 0;
        for (auto it = data.perMinute.cbegin(); it != data.perMinute.cend();
             ++it)
        {
            if (it.value() > peak)
            {
                peak = it.value();
                peakMinute = it.key();
            }
        }

        auto *grid = new QGridLayout;
        grid->setSpacing(10);
        grid->addWidget(tile(number(data.messages), uiText("messages")), 0, 0);
        grid->addWidget(tile(number(data.chatters.size()), uiText("chatters")),
                        0, 1);
        grid->addWidget(
            tile(number(data.firstMessages), uiText("new in the chat")), 0,
            2);
        grid->addWidget(
            tile(QLocale().toString(double(data.messages) / double(minutes), 'f',
                                    1),
                 uiText("messages per minute"),
                 peak > 0 ? uiText("peak %1 at %2")
                                .arg(number(peak),
                                     timeText(QDateTime::fromSecsSinceEpoch(
                                         peakMinute * 60)))
                          : QString()),
            1, 0);
        grid->addWidget(tile(number(data.subscriptions),
                             uiText("subscriptions and gifts")),
                        1, 1);
        grid->addWidget(tile(number(data.moderation),
                             uiText("bans, timeouts and deletions")),
                        1, 2);
        for (int column = 0; column < 3; ++column)
        {
            grid->setColumnStretch(column, 1);
        }
        layout->addLayout(grid);
    }

    // activity
    if (!data.perMinute.isEmpty())
    {
        const auto from = data.since.toSecsSinceEpoch() / 60;
        const auto to = data.live ? QDateTime::currentSecsSinceEpoch() / 60
                                  : data.perMinute.lastKey();
        layout->addWidget(section(
            uiText("Activity"),
            new ActivityChart(data.perMinute, std::min(from, data.perMinute.firstKey()),
                              std::max(to, data.perMinute.lastKey()))));
    }

    // the most active chatters and the most used emotes
    {
        auto *columns = new QHBoxLayout;
        columns->setSpacing(14);

        auto *chatters = new QWidget;
        auto *chattersLayout = new QVBoxLayout(chatters);
        chattersLayout->setContentsMargins(0, 0, 0, 0);
        chattersLayout->setSpacing(4);
        const auto topChatters =
            top(data.chatters, [](const ChannelStatsData::Chatter &chatter) {
                return chatter.messages;
            });
        int place = 1;
        for (const auto &[login, chatter] : topChatters)
        {
            chattersLayout->addWidget(new RankRow(
                place++, chatter.name,
                readable(chatter.color), chatter.messages,
                double(chatter.messages) /
                    double(topChatters.front().second.messages)));
        }
        if (topChatters.isEmpty())
        {
            chattersLayout->addWidget(
                label(uiText("No messages yet."), QStringLiteral("note")));
        }
        chattersLayout->addStretch(1);

        auto *emotes = new QWidget;
        auto *emotesLayout = new QVBoxLayout(emotes);
        emotesLayout->setContentsMargins(0, 0, 0, 0);
        emotesLayout->setSpacing(4);
        const auto topEmotes =
            top(data.emotes, [](const ChannelStatsData::EmoteUse &use) {
                return use.uses;
            });
        place = 1;
        for (const auto &[name, use] : topEmotes)
        {
            QPixmap picture;
            if (use.emote)
            {
                if (auto pixmap = use.emote->images.getImage(2.F)->pixmapOrLoad())
                {
                    picture = *pixmap;
                }
                else
                {
                    waiting = true;
                }
            }
            emotesLayout->addWidget(new RankRow(
                place++, name, QColor(), use.uses,
                double(use.uses) / double(topEmotes.front().second.uses),
                picture));
        }
        if (topEmotes.isEmpty())
        {
            emotesLayout->addWidget(
                label(uiText("No emotes yet."), QStringLiteral("note")));
        }
        emotesLayout->addStretch(1);

        columns->addWidget(section(uiText("Most active chatters"), chatters), 1);
        columns->addWidget(section(uiText("Most used emotes"), emotes), 1);
        layout->addLayout(columns);
    }

    // first messages in the channel
    if (!data.newChatters.isEmpty())
    {
        QStringList names;
        for (const auto &login : data.newChatters.mid(0, 60))
        {
            const auto chatter = data.chatters.value(login);
            const auto color = readable(chatter.color);
            names.append(QStringLiteral("<span style='color:%1'>%2</span>")
                             .arg(color.name(),
                                  (chatter.name.isEmpty() ? login
                                                          : chatter.name)
                                      .toHtmlEscaped()));
        }
        if (data.firstMessages > 60)
        {
            names.append(QStringLiteral("…"));
        }
        auto *box = card();
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(14, 10, 14, 12);
        auto *list = label(names.join(QStringLiteral(" · ")),
                           QStringLiteral("names"));
        list->setTextFormat(Qt::RichText);
        list->setWordWrap(true);
        list->setTextInteractionFlags(Qt::TextSelectableByMouse);
        boxLayout->addWidget(list);
        layout->addWidget(
            section(uiText("First messages in the channel"), box));
    }

    layout->addStretch(1);
    return content;
}

QString styleSheet()
{
    return QStringLiteral(
               "QDialog, QScrollArea, QWidget#content { background: %1; }"
               "QLabel { color: %2; background: transparent; }"
               "QWidget#card { background: %3; border-radius: 10px; }"
               "QLabel#channel { font-size: 21px; font-weight: 700; }"
               "QLabel#live { color: white; background: #e5484d; "
               "border-radius: 4px; padding: 1px 7px; font-size: 11px; "
               "font-weight: 700; }"
               "QLabel#subtitle { color: %4; font-size: 13px; }"
               "QLabel#tileValue { font-size: 22px; font-weight: 700; }"
               "QLabel#tileCaption { color: %4; font-size: 12px; }"
               "QLabel#tileNote { color: %5; font-size: 11px; }"
               "QLabel#section { color: %4; font-size: 12px; font-weight: 700; }"
               "QLabel#note { color: %4; font-size: 12px; }"
               "QLabel#names { font-size: 13px; }"
               "QProgressBar { background: #2a2935; border: none; "
               "border-radius: 2px; }"
               "QProgressBar::chunk { background: %6; border-radius: 2px; }"
               "QScrollBar:vertical { background: transparent; width: 10px; }"
               "QScrollBar::handle:vertical { background: #34323f; "
               "border-radius: 4px; min-height: 30px; margin: 2px; }"
               "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
               "QScrollBar::add-page, QScrollBar::sub-page { background: none; }")
        .arg(BACKGROUND.name(), TEXT.name(), CARD.name(), MUTED.name(),
             FAINT.name(), accent().name());
}

}  // namespace

void showStreamStatsDialog(QWidget *parent, const QString &channelName)
{
    static QHash<QString, QPointer<QDialog>> open;
    const auto key = channelName.toLower();
    if (auto existing = open.value(key))
    {
        existing->showNormal();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *dialog = new QDialog(parent);
    open.insert(key, dialog);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(uiText("Chat statistics") + QStringLiteral(" — ") +
                           channelName);
    dialog->resize(640, 780);
    dialog->setMinimumWidth(520);
    dialog->setStyleSheet(styleSheet());

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll);

    auto *timer = new QTimer(dialog);
    const auto refresh = [scroll, channelName, timer] {
        const auto position = scroll->verticalScrollBar()->value();
        bool waiting = false;
        scroll->setUpdatesEnabled(false);
        delete scroll->takeWidget();
        scroll->setWidget(build(channelName, waiting));
        scroll->verticalScrollBar()->setValue(position);
        scroll->setUpdatesEnabled(true);
        // sooner while emote pictures load
        timer->start(waiting ? 1500 : 5000);
    };
    QObject::connect(timer, &QTimer::timeout, dialog, refresh);
    refresh();

    dialog->show();
    dialog->raise();
}

}  // namespace chatterino
