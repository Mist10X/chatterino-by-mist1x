// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/MenuStyle.hpp"

#include "singletons/Theme.hpp"

#include <QAction>
#include <QMenu>

namespace chatterino {

namespace {

QString colorText(QColor color, double alpha)
{
    color.setAlphaF(float(alpha));
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString menuStyleSheet()
{
    const bool light = getTheme()->isLightTheme();
    const auto accent = getTheme()->accent;
    const QString mode = light ? QStringLiteral("light") : QStringLiteral("dark");
    return QStringLiteral(
               "QMenu { background: %1; border: 1px solid %2; "
               "border-radius: 9px; padding: 5px; }"
               "QMenu::item { color: %3; background: transparent; "
               "padding: 6px 26px 6px 28px; border-radius: 6px; }"
               "QMenu::item:selected { background: %4; }"
               "QMenu::item:disabled { color: %5; }"
               "QMenu::separator { height: 1px; background: %2; "
               "margin: 4px 10px; }"
               "QMenu::indicator { width: 16px; height: 16px; left: 7px; }"
               "QMenu::indicator:checked { image: url(:/buttons/"
               "menu-check-%6.svg); }"
               "QMenu::right-arrow { image: url(:/buttons/menu-arrow-%6.svg); "
               "width: 14px; height: 14px; right: 6px; }")
        .arg(light ? QStringLiteral("#ffffff") : QStringLiteral("#1e1d26"),
             light ? QStringLiteral("#dedce6") : QStringLiteral("#34323f"),
             light ? QStringLiteral("#1b1a22") : QStringLiteral("#e8e6f0"),
             colorText(accent, light ? 0.18 : 0.28),
             light ? QStringLiteral("#a3a1ad") : QStringLiteral("#6c6a7a"),
             mode);
}

/// "Open &stream" -> "Open stream": the underlined letters only clutter a
/// menu opened with the mouse ("&&" stays a plain "&")
QString withoutMnemonic(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (text[i] == u'&')
        {
            if (i + 1 < text.size() && text[i + 1] == u'&')
            {
                out += QStringLiteral("&&");
                ++i;
            }
            continue;
        }
        out += text[i];
    }
    return out;
}

}  // namespace

void styleMenu(QMenu *menu)
{
    if (menu == nullptr || menu->property("mist1xStyled").toBool())
    {
        return;
    }
    menu->setProperty("mist1xStyled", true);
    // rounded corners: the window itself is see-through
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setStyleSheet(menuStyleSheet());
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] {
        for (auto *action : menu->actions())
        {
            if (action->text().contains(u'&'))
            {
                action->setText(withoutMnemonic(action->text()));
            }
        }
        for (auto *child :
             menu->findChildren<QMenu *>(Qt::FindDirectChildrenOnly))
        {
            styleMenu(child);
        }
    });
}

}  // namespace chatterino
