// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringView>
#include <QLatin1StringView>

namespace chatterino {

// Explicit UI call sites only. Never run this over messages, usernames,
// channels, emotes, URLs, stored commands or arbitrary widget trees.
QString uiText(const QString &source);
inline QString uiText(const char *source)
{
    return uiText(QString::fromUtf8(source));
}
inline QString uiText(QStringView source)
{
    return uiText(source.toString());
}
inline QString uiText(QLatin1StringView source)
{
    return uiText(QString(source));
}

void initializeUiLanguage(const QString &language);
QString activeUiLanguage();

}  // namespace chatterino
