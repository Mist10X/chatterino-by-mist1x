// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <vector>

class QPropertyAnimation;
class QTimer;

namespace chatterino {

/// What a corner banner announces. Raids are lavender with the raider's
/// avatar, mass gifted subs green with a gift icon (as in Chatterino+).
struct CornerBannerData {
    enum class Kind : std::uint8_t { Raid, GiftSubs };

    Kind kind = Kind::Raid;
    QString login;
    QString displayName;
    int amount = 0;
    int seconds = 40;
};

/// A card in the top-right corner of a chat view. Stays `seconds`, or until
/// clicked, then slides up out of view and deletes itself.
class CornerBanner final : public QWidget
{
public:
    CornerBanner(const CornerBannerData &data, QWidget *parent);

    QSize sizeHint() const override;
    void dismiss();
    bool isLeaving() const;

    /// Display scale of the chat view this banner sits on.
    void setScale(float scale);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPixmap avatar(int size) const;
    QFont font(qreal points, bool bold) const;
    int margin() const;

    CornerBannerData data_;
    float scale_ = 1.F;
    bool hovered_ = false;
    bool leaving_ = false;
    QTimer *lifetime_;
    QPropertyAnimation *slide_ = nullptr;
};

/// Keeps the banners of one chat view stacked in its top-right corner.
class CornerBannerStack final : public QObject
{
public:
    explicit CornerBannerStack(QWidget *view);

    void show(const CornerBannerData &data);
    void setScale(float scale);
    void clear();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void relayout();

    QWidget *view_;
    float scale_ = 1.F;
    std::vector<QPointer<CornerBanner>> banners_;
};

}  // namespace chatterino
