// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/helper/CornerBanners.hpp"

#include "Application.hpp"
#include "singletons/Fonts.hpp"
#include "widgets/helper/TabAvatars.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QFontMetricsF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QSvgRenderer>
#include <QTimer>

#include <algorithm>

namespace chatterino {

namespace {

struct Palette {
    QColor background;
    QColor glow;
    QColor label;
    QColor text;
    QColor iconBox;
};

Palette paletteFor(CornerBannerData::Kind kind)
{
    if (kind == CornerBannerData::Kind::GiftSubs)
    {
        return {
            .background = QColor(0x1d, 0xc4, 0x1d),
            .glow = QColor(0x1d, 0xe0, 0x1d),
            .label = QColor(0x0b, 0x52, 0x0b),
            .text = QColor(0x0b, 0x17, 0x0b),
            .iconBox = QColor(0x10, 0x8c, 0x10),
        };
    }
    return {
        .background = QColor(0xb8, 0xa4, 0xee),
        .glow = QColor(0xa0, 0x86, 0xf0),
        .label = QColor(0x5b, 0x4d, 0x82),
        .text = QColor(0x17, 0x11, 0x2b),
        .iconBox = QColor(0x8a, 0x73, 0xd1),
    };
}

QString labelFor(CornerBannerData::Kind kind)
{
    return kind == CornerBannerData::Kind::GiftSubs
               ? uiText(QStringLiteral("ПОДПИСКИ"))
               : uiText(QStringLiteral("РЕЙД"));
}

QString amountText(int amount)
{
    return u'+' + QLocale(QLocale::Russian).toString(amount);
}

QString nameFor(const CornerBannerData &data)
{
    if (data.kind == CornerBannerData::Kind::GiftSubs &&
        (data.login.compare(QStringLiteral("ananonymousgifter"),
                            Qt::CaseInsensitive) == 0 ||
         data.displayName.isEmpty()))
    {
        return uiText(QStringLiteral("Аноним"));
    }
    return data.displayName.isEmpty() ? data.login : data.displayName;
}

// Logical sizes at scale 1; everything is multiplied by the view's scale.
constexpr qreal GLOW = 5;
constexpr qreal PAD_X = 9;
constexpr qreal PAD_Y = 6;
constexpr qreal ICON = 28;
constexpr qreal GAP = 9;
constexpr qreal AMOUNT_GAP = 18;
constexpr qreal RADIUS = 8;
constexpr qreal LABEL_PT = 6.5;
constexpr qreal NAME_PT = 10.5;
constexpr qreal AMOUNT_PT = 12.5;

}  // namespace

CornerBanner::CornerBanner(const CornerBannerData &data, QWidget *parent)
    : QWidget(parent)
    , data_(data)
    , lifetime_(new QTimer(this))
{
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setCursor(Qt::PointingHandCursor);
    this->setToolTip(
        data.kind == CornerBannerData::Kind::GiftSubs
            ? uiText(QStringLiteral("%1 дарит %2 подписок · клик — закрыть"))
                  .arg(nameFor(data), QLocale(QLocale::Russian)
                                          .toString(data.amount))
            : uiText(QStringLiteral("Рейд от %1: %2 зрителей · клик — закрыть"))
                  .arg(nameFor(data), QLocale(QLocale::Russian)
                                          .toString(data.amount)));

    this->lifetime_->setSingleShot(true);
    QObject::connect(this->lifetime_, &QTimer::timeout, this, [this] {
        this->dismiss();
    });
    this->lifetime_->start(std::max(1, data.seconds) * 1000);

    if (data.kind == CornerBannerData::Kind::Raid && !data.login.isEmpty())
    {
        QObject::connect(&TabAvatars::instance(), &TabAvatars::loaded, this,
                         [this](const QString &login) {
                             if (login.compare(this->data_.login,
                                               Qt::CaseInsensitive) == 0)
                             {
                                 this->update();
                             }
                         });
    }
}

void CornerBanner::setScale(float scale)
{
    this->scale_ = std::max(0.5F, scale);
    this->resize(this->sizeHint());
    this->update();
}

QFont CornerBanner::font(qreal points, bool bold) const
{
    auto font = getApp()->getFonts()->getFont(FontStyle::ChatMediumBold, 1.0);
    font.setPointSizeF(points * this->scale_);
    font.setWeight(bold ? QFont::Bold : QFont::DemiBold);
    return font;
}

int CornerBanner::margin() const
{
    return int(GLOW * this->scale_);
}

QSize CornerBanner::sizeHint() const
{
    const auto s = qreal(this->scale_);
    auto labelFont = this->font(LABEL_PT, true);
    labelFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8 * s);
    const QFontMetricsF label(labelFont);
    const QFontMetricsF name(this->font(NAME_PT, true));
    const QFontMetricsF amount(this->font(AMOUNT_PT, true));

    const auto textWidth =
        std::max(label.horizontalAdvance(labelFor(this->data_.kind)),
                 name.horizontalAdvance(nameFor(this->data_)));
    const auto width = PAD_X * s + ICON * s + GAP * s + textWidth +
                       AMOUNT_GAP * s +
                       amount.horizontalAdvance(amountText(this->data_.amount)) +
                       PAD_X * s * 1.4;
    const auto textHeight = label.height() + name.height() - 2 * s;
    const auto height = 2 * PAD_Y * s + std::max(ICON * s, textHeight);
    return QSize(int(width + 2 * this->margin() + 0.5),
                 int(height + 2 * this->margin() + 0.5));
}

QPixmap CornerBanner::avatar(int size) const
{
    if (this->data_.kind != CornerBannerData::Kind::Raid ||
        this->data_.login.isEmpty())
    {
        return {};
    }
    return TabAvatars::instance().circle(this->data_.login, size);
}

void CornerBanner::paintEvent(QPaintEvent * /*event*/)
{
    const auto s = qreal(this->scale_);
    const auto colors = paletteFor(this->data_.kind);
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);

    const QRectF card =
        QRectF(this->rect()).adjusted(this->margin(), this->margin(),
                                      -this->margin(), -this->margin());
    const auto radius = RADIUS * s;

    // soft glow around the card
    for (int i = 4; i >= 1; --i)
    {
        auto glow = colors.glow;
        glow.setAlpha(this->hovered_ ? 34 / i + 10 : 26 / i + 6);
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        const auto grow = i * GLOW * s / 4;
        painter.drawRoundedRect(card.adjusted(-grow, -grow, grow, grow),
                                radius + grow, radius + grow);
    }

    painter.setBrush(this->hovered_ ? colors.background.lighter(106)
                                    : colors.background);
    painter.drawRoundedRect(card, radius, radius);

    // icon: raider avatar or gift box
    const QRectF icon(card.left() + PAD_X * s,
                      card.center().y() - ICON * s / 2, ICON * s, ICON * s);
    const auto avatar = this->avatar(int(icon.width() * this->devicePixelRatioF()));
    if (!avatar.isNull())
    {
        painter.drawPixmap(icon, avatar, QRectF(avatar.rect()));
    }
    else if (this->data_.kind == CornerBannerData::Kind::Raid)
    {
        painter.setBrush(colors.iconBox);
        painter.drawEllipse(icon);
        painter.setPen(Qt::white);
        painter.setFont(this->font(NAME_PT, true));
        painter.drawText(icon, Qt::AlignCenter,
                         nameFor(this->data_).left(1).toUpper());
    }
    else
    {
        painter.setBrush(colors.iconBox);
        painter.drawRoundedRect(icon, 6 * s, 6 * s);
        static QSvgRenderer gift(
            QStringLiteral(":/buttons/usercardGift-darkMode.svg"));
        const auto inset = icon.width() * 0.22;
        gift.render(&painter, icon.adjusted(inset, inset, -inset, -inset));
    }

    // texts
    auto labelFont = this->font(LABEL_PT, true);
    labelFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8 * s);
    const auto nameFont = this->font(NAME_PT, true);
    const auto amountFont = this->font(AMOUNT_PT, true);
    const QFontMetricsF label(labelFont);
    const QFontMetricsF name(nameFont);
    const QFontMetricsF amount(amountFont);

    const auto amountString = amountText(this->data_.amount);
    const auto amountWidth = amount.horizontalAdvance(amountString);
    const auto right = card.right() - PAD_X * s * 1.4;
    const auto textLeft = icon.right() + GAP * s;
    const auto textWidth = right - amountWidth - AMOUNT_GAP * s - textLeft;
    const auto blockHeight = label.height() + name.height() - 2 * s;
    const auto top = card.center().y() - blockHeight / 2;

    painter.setPen(colors.label);
    painter.setFont(labelFont);
    painter.drawText(QPointF(textLeft, top + label.ascent()),
                     labelFor(this->data_.kind));

    painter.setPen(colors.text);
    painter.setFont(nameFont);
    painter.drawText(
        QPointF(textLeft, top + label.height() - 2 * s + name.ascent()),
        name.elidedText(nameFor(this->data_), Qt::ElideRight, textWidth));

    painter.setFont(amountFont);
    painter.drawText(QPointF(right - amountWidth,
                             card.center().y() - amount.height() / 2 +
                                 amount.ascent()),
                     amountString);
}

void CornerBanner::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->rect().contains(event->position().toPoint()))
    {
        this->dismiss();
    }
}

void CornerBanner::enterEvent(QEnterEvent * /*event*/)
{
    this->hovered_ = true;
    this->update();
}

void CornerBanner::leaveEvent(QEvent * /*event*/)
{
    this->hovered_ = false;
    this->update();
}

bool CornerBanner::isLeaving() const
{
    return this->leaving_;
}

void CornerBanner::dismiss()
{
    if (this->leaving_)
    {
        return;
    }
    this->leaving_ = true;
    this->lifetime_->stop();
    this->setAttribute(Qt::WA_TransparentForMouseEvents);

    this->slide_ = new QPropertyAnimation(this, "pos", this);
    this->slide_->setDuration(260);
    this->slide_->setStartValue(this->pos());
    this->slide_->setEndValue(QPoint(this->x(), -this->height()));
    this->slide_->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(this->slide_, &QPropertyAnimation::finished, this,
                     &QObject::deleteLater);
    this->slide_->start();
}

CornerBannerStack::CornerBannerStack(QWidget *view)
    : QObject(view)
    , view_(view)
{
    view->installEventFilter(this);
}

void CornerBannerStack::show(const CornerBannerData &data)
{
    auto *banner = new CornerBanner(data, this->view_);
    banner->setScale(this->scale_);
    QObject::connect(banner, &QObject::destroyed, this, [this] {
        this->relayout();
    });
    this->banners_.emplace_back(banner);
    this->relayout();

    // slide in from above the view
    const auto target = banner->pos();
    banner->move(target.x(), -banner->height());
    banner->show();
    banner->raise();
    auto *slide = new QPropertyAnimation(banner, "pos", banner);
    slide->setObjectName(QStringLiteral("slideIn"));
    slide->setDuration(220);
    slide->setStartValue(banner->pos());
    slide->setEndValue(target);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    slide->start(QAbstractAnimation::DeleteWhenStopped);
}

void CornerBannerStack::setScale(float scale)
{
    this->scale_ = scale;
    for (const auto &banner : this->banners_)
    {
        if (banner)
        {
            banner->setScale(scale);
        }
    }
    this->relayout();
}

void CornerBannerStack::clear()
{
    for (const auto &banner : this->banners_)
    {
        if (banner)
        {
            banner->deleteLater();
        }
    }
    this->banners_.clear();
}

bool CornerBannerStack::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->view_ && event->type() == QEvent::Resize)
    {
        this->relayout();
    }
    return QObject::eventFilter(watched, event);
}

void CornerBannerStack::relayout()
{
    std::erase_if(this->banners_, [](const auto &banner) {
        return banner.isNull();
    });

    const auto edge = int(8 * this->scale_);
    const auto gap = int(2 * this->scale_);
    // the newest banner is on top, older ones move down below it
    int y = edge;
    for (auto it = this->banners_.rbegin(); it != this->banners_.rend(); ++it)
    {
        auto *banner = it->data();
        if (banner->isLeaving())
        {
            continue;
        }
        // a newer banner pushes this one down: end its slide-in right away
        if (auto *slideIn = banner->findChild<QPropertyAnimation *>(
                QStringLiteral("slideIn")))
        {
            slideIn->stop();
        }
        const auto size = banner->sizeHint();
        banner->resize(size);
        banner->move(this->view_->width() - size.width() - edge, y);
        y += size.height() + gap;
    }
}

}  // namespace chatterino
