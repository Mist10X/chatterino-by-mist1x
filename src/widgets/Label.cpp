// SPDX-FileCopyrightText: 2019 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/Label.hpp"

#include "Application.hpp"

#include <QPainter>

namespace chatterino {

Label::Label(QString text, FontStyle style)
    : Label(nullptr, std::move(text), style)
{
}

Label::Label(BaseWidget *parent, QString text, FontStyle style)
    : BaseWidget(parent)
    , text_(std::move(text))
    , fontStyle_(style)
    , basePadding_(8, 0, 8, 0)
{
    this->connections_.managedConnect(getApp()->getFonts()->fontChanged,
                                      [this] {
                                          this->updateSize();
                                      });
    this->updateSize();
}

const QString &Label::getText() const
{
    return this->text_;
}

void Label::setText(const QString &text)
{
    if (this->text_ != text)
    {
        this->text_ = text;
        if (this->shouldElide_)
        {
            this->updateElidedText(this->getFontMetrics(),
                                   this->textRect().width());
        }
        this->updateSize();
        this->update();
    }
}

FontStyle Label::getFontStyle() const
{
    return this->fontStyle_;
}

bool Label::getCentered() const
{
    return this->centered_;
}

void Label::setCentered(bool centered)
{
    this->centered_ = centered;
    this->updateSize();
}

void Label::setPadding(QMargins padding)
{
    this->basePadding_ = padding;
    this->updateSize();
}

bool Label::getWordWrap() const
{
    return this->wordWrap_;
}

void Label::setWordWrap(bool wrap)
{
    this->wordWrap_ = wrap;
    this->update();
}

void Label::setShouldElide(bool shouldElide)
{
    this->shouldElide_ = shouldElide;
    this->updateSize();
    this->update();
}

void Label::setElideSuffix(const QString &suffix)
{
    if (this->elideSuffix_ == suffix)
    {
        return;
    }
    this->elideSuffix_ = suffix;
    if (this->shouldElide_)
    {
        this->updateElidedText(this->getFontMetrics(),
                               this->textRect().width());
    }
    this->updateSize();
    this->update();
}

void Label::setDetails(const QString &details)
{
    if (this->details_ == details)
    {
        return;
    }
    this->details_ = details;
    this->updateSize();
    this->update();
}

void Label::setLeadingDot(const QColor &color)
{
    if (this->dotColor_ == color)
    {
        return;
    }
    this->dotColor_ = color;
    this->updateSize();
    this->update();
}

void Label::paintWithDetails(QPainter &painter, const QFontMetricsF &metrics)
{
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = this->textRect();
    const qreal dot = this->dotColor_.isValid() ? metrics.height() * 0.42 : 0;
    const qreal dotGap = dot > 0 ? metrics.height() * 0.38 : 0;
    const qreal gap = this->details_.isEmpty() ? 0 : metrics.height() * 0.55;
    qreal available = area.width() - dot - dotGap;

    QString main = this->text_;
    QString details = this->details_;
    qreal mainWidth = metrics.horizontalAdvance(main);
    // The details give way first, then the name.
    qreal detailsWidth = 0;
    if (!details.isEmpty())
    {
        details = metrics.elidedText(details, Qt::ElideRight,
                                     std::max<qreal>(0, available - mainWidth - gap));
        if (details.size() <= 2)
        {
            details.clear();
        }
        detailsWidth = details.isEmpty() ? 0 : metrics.horizontalAdvance(details);
    }
    if (mainWidth + (details.isEmpty() ? 0 : gap + detailsWidth) > available)
    {
        main = metrics.elidedText(main, Qt::ElideRight, available);
        mainWidth = metrics.horizontalAdvance(main);
    }
    const qreal total =
        dot + dotGap + mainWidth + (details.isEmpty() ? 0 : gap + detailsWidth);
    qreal x = this->centered_ && total < area.width()
                  ? area.left() + (area.width() - total) / 2
                  : area.left();
    const qreal baseline =
        area.top() + (area.height() - metrics.height()) / 2 + metrics.ascent();

    if (dot > 0)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(this->dotColor_);
        painter.drawEllipse(QRectF(x, area.center().y() - dot / 2, dot, dot));
        x += dot + dotGap;
    }
    const auto text = this->palette().windowText().color();
    painter.setPen(text);
    painter.drawText(QPointF(x, baseline), main);
    x += mainWidth;
    if (!details.isEmpty())
    {
        auto muted = text;
        muted.setAlphaF(text.alphaF() * 0.62);
        painter.setPen(muted);
        painter.drawText(QPointF(x + gap, baseline), details);
    }
}

void Label::setFontStyle(FontStyle style)
{
    this->fontStyle_ = style;
    this->updateSize();
}

void Label::scaleChangedEvent(float)
{
    this->updateSize();
}

QSize Label::sizeHint() const
{
    return this->sizeHint_;
}

QSize Label::minimumSizeHint() const
{
    return this->minimumSizeHint_;
}

void Label::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    auto metrics = this->getFontMetrics();

    painter.setFont(
        getApp()->getFonts()->getFont(this->getFontStyle(), this->scale()));

    QRectF textRect = this->textRect();

    auto text = [this] {
        if (this->shouldElide_)
        {
            return this->elidedText_;
        }

        return this->text_;
    }();

    qreal width = metrics.horizontalAdvance(text);
    Qt::Alignment alignment = !this->centered_ || width > textRect.width()
                                  ? Qt::AlignLeft | Qt::AlignVCenter
                                  : Qt::AlignCenter;

    painter.setBrush(this->palette().windowText());

    if (!this->details_.isEmpty() || this->dotColor_.isValid())
    {
        this->paintWithDetails(painter, metrics);
        return;
    }

    QTextOption option(alignment);
    if (this->wordWrap_)
    {
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    }
    else
    {
        option.setWrapMode(QTextOption::NoWrap);
    }
    painter.drawText(textRect, text, option);

#if 0
    painter.setPen(QColor(255, 0, 0));
    painter.drawRect(0, 0, this->width() - 1, this->height() - 1);
#endif
}

void Label::resizeEvent(QResizeEvent *event)
{
    if (this->shouldElide_)
    {
        auto metrics = this->getFontMetrics();
        if (this->updateElidedText(metrics, this->textRect().width()))
        {
            this->update();
        }
    }

    BaseWidget::resizeEvent(event);
}

QFontMetricsF Label::getFontMetrics() const
{
    return getApp()->getFonts()->getFontMetrics(this->fontStyle_,
                                                this->scale());
}

void Label::updateSize()
{
    this->currentPadding_ = this->basePadding_.toMarginsF() * this->scale();

    auto metrics = this->getFontMetrics();

    auto yPadding =
        this->currentPadding_.top() + this->currentPadding_.bottom();

    auto height = metrics.height() + yPadding;
    if (this->shouldElide_)
    {
        this->updateElidedText(metrics, this->textRect().width());
        this->sizeHint_ = QSizeF(-1, height).toSize();
        if (this->elideSuffix_.isEmpty())
        {
            this->minimumSizeHint_ = this->sizeHint_;
        }
        else
        {
            auto minWidth = metrics.horizontalAdvance(this->elideSuffix_) +
                            this->currentPadding_.left() +
                            this->currentPadding_.right();
            this->minimumSizeHint_ = QSizeF(minWidth, height).toSize();
        }
    }
    else
    {
        auto width = metrics.horizontalAdvance(this->text_) +
                     this->currentPadding_.left() +
                     this->currentPadding_.right();
        if (this->dotColor_.isValid())
        {
            width += metrics.height() * 0.8;
        }
        auto minimum = width;
        if (!this->details_.isEmpty())
        {
            width += metrics.height() * 0.55 +
                     metrics.horizontalAdvance(this->details_);
        }
        this->sizeHint_ = QSizeF(width, height).toSize();
        // The details may be cut: only the name has to fit.
        this->minimumSizeHint_ =
            this->details_.isEmpty() && !this->dotColor_.isValid()
                ? this->sizeHint_
                : QSizeF(std::min(minimum, metrics.height() * 6), height)
                      .toSize();
    }

    this->updateGeometry();
}

bool Label::updateElidedText(const QFontMetricsF &fontMetrics, qreal width)
{
    assert(this->shouldElide_ == true);
    QString elidedText;
    if (this->elideSuffix_.isEmpty())
    {
        elidedText = fontMetrics.elidedText(
            this->text_, Qt::TextElideMode::ElideRight, width);
    }
    else
    {
        auto suffixWidth = fontMetrics.horizontalAdvance(this->elideSuffix_);
        elidedText =
            fontMetrics.elidedText(this->text_, Qt::TextElideMode::ElideRight,
                                   qMax(qreal(0), width - suffixWidth)) +
            this->elideSuffix_;
    }

    if (elidedText != this->elidedText_)
    {
        this->elidedText_ = elidedText;
        return true;
    }

    return false;
}

QRectF Label::textRect() const
{
    return QRectF(this->rect()).marginsRemoved(this->currentPadding_);
}

}  // namespace chatterino
