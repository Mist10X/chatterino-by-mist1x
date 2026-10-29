// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/UpdateBanner.hpp"

#include "Application.hpp"
#include "common/UiLanguage.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/Updates.hpp"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>

namespace chatterino {

namespace {

/// The version whose card was closed; kept in Misc, next to the settings.
QString dismissedFile()
{
    return QDir(getApp()->getPaths().miscDirectory)
        .filePath("update-dismissed.txt");
}

QString dismissedVersion()
{
    QFile file(dismissedFile());
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

void setDismissedVersion(const QString &version)
{
    QFile file(dismissedFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(version.toUtf8());
    }
}

class Banner final : public QWidget
{
public:
    explicit Banner(QWidget *window)
        : QWidget(window)
        , window_(window)
    {
        this->setMouseTracking(true);
        this->setCursor(Qt::PointingHandCursor);
        window->installEventFilter(this);
    }

    float scale() const
    {
        return std::max(0.5F, getSettings()->getClampedUiScale());
    }

    QSize sizeHint() const override
    {
        const auto s = this->scale();
        const int available = this->window_->width() - int(24 * s);
        return {std::min(int(420 * s), std::max(int(220 * s), available)),
                int(58 * s)};
    }

    void place()
    {
        const auto s = this->scale();
        const auto size = this->sizeHint();
        this->setGeometry((this->window_->width() - size.width()) / 2,
                          int(36 * s), size.width(), size.height());
        this->raise();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == this->window_ && event->type() == QEvent::Resize &&
            this->isVisible())
        {
            this->place();
        }
        return false;
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        const auto s = this->scale();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool light = getTheme()->isLightTheme();
        const auto accent = getTheme()->accent;
        painter.setPen(QPen(accent.darker(light ? 110 : 160), 1));
        painter.setBrush(QColor(
            light ? "#f4f0fb" : (this->hovered_ ? "#2a2535" : "#221f2b")));
        painter.drawRoundedRect(card, 8 * s, 8 * s);
        QPainterPath strip;
        strip.addRoundedRect(
            QRectF(card.left(), card.top(), 4 * s, card.height()), 2 * s,
            2 * s);
        painter.fillPath(strip, accent);

        auto *fonts = getApp()->getFonts();
        const qreal pad = 12 * s;
        const qreal closeSize = 16 * s;
        const qreal textWidth = card.width() - 2 * pad - closeSize;
        const auto bold = fonts->getFont(FontStyle::ChatMediumBold, s);
        const auto normal = fonts->getFont(FontStyle::ChatMedium, s);
        const QFontMetricsF boldMetrics(bold);
        const QFontMetricsF normalMetrics(normal);

        const qreal top = card.top() + 8 * s;
        painter.setFont(bold);
        painter.setPen(light ? QColor("#3c2a63") : QColor("#f0ecfa"));
        painter.drawText(
            QPointF(card.left() + pad, top + boldMetrics.ascent()),
            boldMetrics.elidedText(
                uiText("Update %1 is available")
                    .arg(getApp()->getUpdates().getOnlineVersion()),
                Qt::ElideRight, textWidth));
        painter.setFont(normal);
        painter.setPen(light ? QColor("#555") : QColor("#b6b2c2"));
        const qreal second = top + boldMetrics.height() + 3 * s;
        painter.drawText(
            QPointF(card.left() + pad, second + normalMetrics.ascent()),
            normalMetrics.elidedText(
                uiText("Click to see what is new and update"), Qt::ElideRight,
                textWidth));

        const QRectF cross(card.right() - pad - closeSize * 0.7,
                           card.top() + 9 * s, closeSize * 0.7,
                           closeSize * 0.7);
        painter.setPen(
            QPen(light ? QColor("#777") : QColor("#9a96a6"), 1.4 * s));
        painter.drawLine(cross.topLeft(), cross.bottomRight());
        painter.drawLine(cross.topRight(), cross.bottomLeft());
        this->closeRect_ = cross.adjusted(-6 * s, -6 * s, 6 * s, 6 * s);
    }

    void enterEvent(QEnterEvent * /*event*/) override
    {
        this->hovered_ = true;
        this->update();
    }

    void leaveEvent(QEvent * /*event*/) override
    {
        this->hovered_ = false;
        this->update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }
        // Closed: not again for this version (the title bar button stays).
        setDismissedVersion(getApp()->getUpdates().getOnlineVersion());
        this->hide();
        if (!this->closeRect_.contains(event->position()))
        {
            showUpdatePrompt(this->window_);
        }
    }

private:
    QWidget *window_;
    bool hovered_ = false;
    QRectF closeRect_;
};

}  // namespace

void showUpdatePrompt(QWidget *parent)
{
    auto &updates = getApp()->getUpdates();
    if (updates.getStatus() == Updates::Downloading)
    {
        QMessageBox::information(
            parent, uiText("Chatterino by Mist1X update"),
            uiText("The update is downloading. The program will restart by "
                   "itself when it is installed."));
        return;
    }

    auto *box = new QMessageBox(parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Information);
    box->setWindowTitle(uiText("Chatterino by Mist1X update"));
    box->setText(updates.buildUpdateAvailableText());

    auto notes = updates.getReleaseNotes();
    if (notes.size() > 1500)
    {
        notes = notes.left(1500) + QStringLiteral("…");
    }
    const bool installed = Updates::isInstalledCopy();
    box->setInformativeText(
        (notes.isEmpty() ? QString() : notes + "\n\n") +
        (installed ? uiText("The update is downloaded and installed; the "
                            "settings stay. The program restarts by itself.")
                   : uiText("This copy is not installed with the setup: the "
                            "download page opens.")));

    auto *update = box->addButton(
        installed ? uiText("Update now") : uiText("Open the download page"),
        QMessageBox::AcceptRole);
    box->addButton(uiText("Later"), QMessageBox::RejectRole);
    box->setDefaultButton(update);
    QObject::connect(update, &QPushButton::clicked, [] {
        getApp()->getUpdates().installUpdates();
    });
    box->show();
    box->raise();
    box->activateWindow();
}

void initUpdateBanner(QWidget *window, pajlada::Signals::SignalHolder &holder)
{
    QPointer<Banner> banner = new Banner(window);
    banner->hide();
    const auto changed = [banner](Updates::Status status) {
        if (banner.isNull())
        {
            return;
        }
        const auto &version = getApp()->getUpdates().getOnlineVersion();
        if (status == Updates::UpdateAvailable &&
            version != dismissedVersion())
        {
            banner->show();
            banner->place();
        }
        else if (status != Updates::UpdateAvailable)
        {
            banner->hide();
        }
    };
    holder.managedConnect(getApp()->getUpdates().statusUpdated, changed);
}

}  // namespace chatterino
