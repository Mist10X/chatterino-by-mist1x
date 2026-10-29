// SPDX-FileCopyrightText: 2019 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Fonts.hpp"

#include "Application.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"

#include <QDebug>
#include <qfont.h>
#include <qfontdatabase.h>
#include <QFontDatabase>
#include <QtGlobal>

namespace {

using namespace chatterino;

// A portable profile can name this app-local family even when Windows has
// never registered it. Keep the selected family/size/weight; make its bundled
// faces available privately before resolving or caching any chat fonts.
void ensureSelectedBundledFont(const QString &family)
{
    if (family.compare(QStringLiteral("Archive Montserrat"), Qt::CaseInsensitive) != 0 ||
        QFontDatabase::families().contains(QStringLiteral("Archive Montserrat"), Qt::CaseInsensitive))
    {
        return;
    }
    for (const auto *style : {"Regular", "SemiBold", "Bold"})
    {
        const auto resource = QStringLiteral(":/fonts/ArchiveMontserrat-") +
                              QLatin1String(style) + QStringLiteral(".ttf");
        if (QFontDatabase::addApplicationFont(resource) < 0)
        {
            qWarning() << "Could not load bundled chat font" << resource;
        }
    }
}

int getUsernameBoldness()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

    static constexpr std::array<std::array<int, 2>, 9> legacyToOpenTypeMap{{
        {0, QFont::Thin},
        {12, QFont::ExtraLight},
        {25, QFont::Light},
        {50, QFont::Normal},
        {57, QFont::Medium},
        {63, QFont::DemiBold},
        {75, QFont::Bold},
        {81, QFont::ExtraBold},
        {87, QFont::Black},
    }};

    const int target = getSettings()->boldScale.getValue();

    int result = QFont::Medium;
    int closestDist = INT_MAX;

    for (const auto [weightOld, weightNew] : legacyToOpenTypeMap)
    {
        const int dist = qAbs(weightOld - target);
        if (dist < closestDist)
        {
            result = weightNew;
            closestDist = dist;
        }
        else
        {
            break;
        }
    }

    return result;
#else
    return getSettings()->boldScale.getValue();
#endif
}

float fontSize(FontStyle style)
{
    auto chatSize = [] {
        return static_cast<float>(getSettings()->chatFontSize);
    };
    switch (style)
    {
        case FontStyle::ChatSmall:
            return 0.6F * chatSize();
        case FontStyle::ChatMediumSmall:
            return 0.8F * chatSize();
        case FontStyle::ChatMedium:
        case FontStyle::ChatMediumBold:
        case FontStyle::ChatMediumItalic:
        case FontStyle::ChatMediumMono:
        case FontStyle::ChatMediumStrikethrough:
            return chatSize();
        case FontStyle::TimestampMedium:
            switch (getSettings()->timestampSize.getEnum())
            {
                case TimestampSize::Small:
                    return 0.8F * chatSize();
                case TimestampSize::Smaller:
                    return 0.7F * chatSize();
                case TimestampSize::SameAsText:
                default:
                    return chatSize();
            }
        case FontStyle::ChatLarge:
            return 1.2F * chatSize();
        case FontStyle::ChatVeryLarge:
            return 1.4F * chatSize();

        case FontStyle::Tiny:
            return 8;
        case FontStyle::UiMedium:
        case FontStyle::UiMediumBold:
        case FontStyle::UiTabs:
        case FontStyle::EndType:
            return 9;
    }

    assert(false);
    return 9;
}

int fontWeight(FontStyle style)
{
    switch (style)
    {
        case FontStyle::ChatSmall:
        case FontStyle::ChatMediumSmall:
        case FontStyle::ChatMedium:
        case FontStyle::ChatMediumMono:
        case FontStyle::ChatMediumStrikethrough:
        case FontStyle::ChatMediumItalic:
        case FontStyle::ChatLarge:
        case FontStyle::ChatVeryLarge:
        case FontStyle::TimestampMedium:
            return getSettings()->chatFontWeight.getValue();

        case FontStyle::ChatMediumBold:
            return getUsernameBoldness();

        case FontStyle::Tiny:
        case FontStyle::UiMedium:
        case FontStyle::EndType:
            return QFont::Normal;

        case FontStyle::UiTabs:
            return QFont::DemiBold;

        case FontStyle::UiMediumBold:
            return QFont::Bold;
    }

    assert(false);
    return QFont::Normal;
}

bool isItalic(FontStyle style)
{
    switch (style)
    {
        case FontStyle::Tiny:
        case FontStyle::ChatSmall:
        case FontStyle::ChatMediumSmall:
        case FontStyle::ChatMedium:
        case FontStyle::ChatMediumMono:
        case FontStyle::ChatMediumStrikethrough:
        case FontStyle::ChatMediumBold:
        case FontStyle::ChatLarge:
        case FontStyle::ChatVeryLarge:
        case FontStyle::TimestampMedium:
        case FontStyle::UiMedium:
        case FontStyle::UiMediumBold:
        case FontStyle::UiTabs:
        case FontStyle::EndType:
            return false;

        case FontStyle::ChatMediumItalic:
            return true;
    }

    assert(false);
    return false;
}

QString fontFamily(FontStyle style)
{
    switch (style)
    {
        case FontStyle::Tiny:
            return QStringLiteral("Monospace");

        case FontStyle::ChatSmall:
        case FontStyle::ChatMediumSmall:
        case FontStyle::ChatMedium:
        case FontStyle::ChatMediumStrikethrough:
        case FontStyle::ChatMediumBold:
        case FontStyle::ChatMediumItalic:
        case FontStyle::ChatLarge:
        case FontStyle::ChatVeryLarge:
        case FontStyle::TimestampMedium:
            return getSettings()->chatFontFamily.getValue();
        case FontStyle::ChatMediumMono:
            QFontDatabase::applicationFontFamilies(getApp()->monoFontId).at(0);
        case FontStyle::UiMedium:
        case FontStyle::UiMediumBold:
        case FontStyle::UiTabs:
            // The interface uses the chat font as well: one typeface for the
            // tabs, headers and the chat instead of the system font next to it.
            return getSettings()->chatFontFamily.getValue();
        case FontStyle::EndType:
            return QStringLiteral(DEFAULT_FONT_FAMILY);
    }

    assert(false);
    return QStringLiteral(DEFAULT_FONT_FAMILY);
}

}  // namespace

namespace chatterino {

Fonts::Fonts(Settings &settings)
{
    ensureSelectedBundledFont(settings.chatFontFamily.getValue());
    this->fontsByType_.resize(size_t(FontStyle::EndType));

    this->fontChangedListener.setCB([this, &settings] {
        assertInGuiThread();
        ensureSelectedBundledFont(settings.chatFontFamily.getValue());

        for (auto &map : this->fontsByType_)
        {
            map.clear();
        }
        this->fontChanged.invoke();
    });
    this->fontChangedListener.addSetting(settings.chatFontFamily);
    this->fontChangedListener.addSetting(settings.chatFontSize);
    this->fontChangedListener.addSetting(settings.chatFontWeight);
    this->fontChangedListener.addSetting(settings.boldScale);
    this->fontChangedListener.addSetting(settings.timestampSize);
}

QFont Fonts::getFont(FontStyle type, float scale)
{
    return this->getOrCreateFontData(type, scale).font;
}

QFontMetricsF Fonts::getFontMetrics(FontStyle type, float scale)
{
    return this->getOrCreateFontData(type, scale).metrics;
}

Fonts::FontData &Fonts::getOrCreateFontData(FontStyle type, float scale)
{
    assertInGuiThread();

    assert(type < FontStyle::EndType);

    auto &map = this->fontsByType_[size_t(type)];

    auto it = map.find(scale);
    if (it != map.end())
    {
        return it->second;
    }

    auto result = map.emplace(scale, Fonts::createFontData(type, scale));
    assert(result.second);

    return result.first->second;
}

Fonts::FontData Fonts::createFontData(FontStyle type, float scale)
{
    QFont font{
        fontFamily(type),
        static_cast<int>(fontSize(type) * scale),
        fontWeight(type),
        isItalic(type),
    };

    font.setStrikeOut(type == FontStyle::ChatMediumStrikethrough);

    switch (type)
    {
        case FontStyle::TimestampMedium: {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

            auto tag = QFont::Tag("tnum");
            font.setFeature(tag, 1);
#endif
        }
        break;

        default:
            break;
    }

    return font;
}

}  // namespace chatterino
