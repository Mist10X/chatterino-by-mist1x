// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/SettingsDialogTab.hpp"

#include "singletons/Theme.hpp"
#include "common/UiLanguage.hpp"

#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/settingspages/SettingsPage.hpp"

#include <QPainter>
#include <QStyleOption>

namespace chatterino {

SettingsDialogTab::SettingsDialogTab(SettingsDialog *_dialog,
                                     std::function<SettingsPage *()> _lazyPage,
                                     const QString &name, QString imageFileName,
                                     SettingsTabId id)
    : BaseWidget(_dialog)
    , dialog_(_dialog)
    , lazyPage_(std::move(_lazyPage))
    , id_(id)
    , name_(uiText(name))
{
    this->ui_.labelText = uiText(name);
    this->ui_.icon.addFile(imageFileName);

    this->setCursor(QCursor(Qt::PointingHandCursor));
    this->setAttribute(Qt::WA_Hover);
}

void SettingsDialogTab::setSelected(bool _selected)
{
    if (this->selected_ == _selected)
    {
        return;
    }

    //    height: <checkbox-size>px;

    this->selected_ = _selected;
    this->update();
    this->selectedChanged(this->selected_);
}

SettingsPage *SettingsDialogTab::page()
{
    if (this->page_)
    {
        return this->page_;
    }

    this->page_ = this->lazyPage_();
    this->page_->setTab(this);
    return this->page_;
}

SettingsPage *SettingsDialogTab::createdPage() const
{
    return this->page_;
}

void SettingsDialogTab::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // a soft pill behind the selected (and hovered) entry, an accent bar at
    // its left edge
    const auto accent = getTheme()->accent;
    const QRectF pill = QRectF(this->rect()).adjusted(8, 1, -6, -1);
    painter.setPen(Qt::NoPen);
    if (this->selected_)
    {
        auto background = accent;
        background.setAlpha(42);
        painter.setBrush(background);
        painter.drawRoundedRect(pill, 7, 7);
        painter.setBrush(accent);
        painter.drawRoundedRect(
            QRectF(pill.left(), pill.top() + 7, 3, pill.height() - 14), 1.5,
            1.5);
    }
    else if (this->underMouse())
    {
        painter.setBrush(QColor(255, 255, 255, 14));
        painter.drawRoundedRect(pill, 7, 7);
    }

    const int iconSize = 18;
    const int iconX = static_cast<int>(pill.left()) + 12;
    painter.setOpacity(this->selected_ ? 1.0 : 0.72);
    painter.drawPixmap(iconX, (this->height() - iconSize) / 2,
                       this->ui_.icon.pixmap(QSize(iconSize, iconSize)));
    painter.setOpacity(1.0);

    const int textX = iconX + iconSize + 11;
    painter.setPen(this->selected_ ? QColor("#ffffff") : QColor("#c4c3cf"));
    painter.setFont(this->font());
    painter.drawText(QRect(textX, 0, this->width() - textX - 6, this->height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     this->fontMetrics().elidedText(this->ui_.labelText,
                                                    Qt::ElideRight,
                                                    this->width() - textX - 6));
}

void SettingsDialogTab::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    this->dialog_->selectTab(this);

    this->setFocus();
}

const QString &SettingsDialogTab::name() const
{
    return this->name_;
}

SettingsTabId SettingsDialogTab::id() const
{
    return this->id_;
}

}  // namespace chatterino
