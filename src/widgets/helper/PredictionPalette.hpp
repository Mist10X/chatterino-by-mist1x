#pragma once

#include "singletons/Theme.hpp"

#include <QColor>
#include <QString>

#include <array>

namespace chatterino::predictionpalette {

/// Soft colours that sit on the dark Mist backgrounds next to the lavender
/// accent; the first three stand for Twitch's blue, pink and green.
inline constexpr std::array<const char *, 10> OUTCOMES = {{
    "#7fa8ff",  // blue
    "#f08cbf",  // pink
    "#6fcf97",  // green
    "#e8b86a",  // amber
    "#b8a7f0",  // lavender
    "#6cc7c0",  // teal
    "#ef8a8a",  // red
    "#b5d47a",  // lime
    "#79c6e6",  // cyan
    "#f0a878",  // orange
}};

inline QColor outcome(int index, const QString &twitchColor = QString())
{
    if (twitchColor == "PINK")
    {
        return QColor(OUTCOMES[1]);
    }
    if (twitchColor == "GREEN")
    {
        return QColor(OUTCOMES[2]);
    }
    if (twitchColor == "BLUE" && index == 0)
    {
        return QColor(OUTCOMES[0]);
    }
    return QColor(OUTCOMES[size_t(std::max(0, index)) % OUTCOMES.size()]);
}

inline QColor won()
{
    return QColor("#6fcf97");
}

inline QColor lost()
{
    return QColor("#ef8a8a");
}

inline QColor locked()
{
    return QColor("#e8b86a");
}

inline QColor muted()
{
    return QColor("#8f8f9f");
}

/// The theme's accent (lavender in Mist-Dark) for an open prediction.
inline QColor accent()
{
    if (auto *theme = getTheme())
    {
        return theme->accent;
    }
    return QColor("#b8a7f0");
}

inline QColor status(const QString &status)
{
    if (status == "ACTIVE")
    {
        return accent();
    }
    if (status == "LOCKED")
    {
        return locked();
    }
    if (status == "RESOLVED")
    {
        return won();
    }
    return muted();
}

}  // namespace chatterino::predictionpalette
