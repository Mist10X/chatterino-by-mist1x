// SPDX-License-Identifier: MIT
#include "common/UiLanguage.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTranslator>

#include <atomic>

namespace chatterino {
namespace {

QString language = QStringLiteral("en");
constinit std::atomic<bool> initialized{false};

class UiTranslator final : public QTranslator
{
public:
    explicit UiTranslator(const QString &code)
    {
        QFile file(QStringLiteral(":/i18n/%1.json").arg(code));
        if (file.open(QIODevice::ReadOnly))
        {
            this->entries_ = QJsonDocument::fromJson(file.readAll()).object();
        }
    }

    QString translate(const char *, const char *source,
                      const char * = nullptr, int = -1) const override
    {
        if (source == nullptr)
        {
            return {};
        }
        const auto value = this->entries_.value(QString::fromUtf8(source));
        return value.isString() ? value.toString() : QString();
    }

    bool isEmpty() const override
    {
        return this->entries_.isEmpty();
    }

private:
    QJsonObject entries_;
};

}  // namespace

void initializeUiLanguage(const QString &selected)
{
    // Don't change QLocale::setDefault: chat timestamps, number parsing and
    // settings serialization keep their established behavior.
    static UiTranslator russian(QStringLiteral("ru"));
    static UiTranslator english(QStringLiteral("en"));
    static QTranslator qtRussian;
    static const bool loaded = qtRussian.load(":/i18n/qtbase_ru.qm");
    Q_UNUSED(loaded);
    QCoreApplication::removeTranslator(&russian);
    QCoreApplication::removeTranslator(&english);
    QCoreApplication::removeTranslator(&qtRussian);
    language = selected == QStringLiteral("ru") ? QStringLiteral("ru")
                                               : QStringLiteral("en");
    if (language == QStringLiteral("ru"))
    {
        QCoreApplication::installTranslator(&qtRussian);
        QCoreApplication::installTranslator(&russian);
    }
    else
    {
        QCoreApplication::installTranslator(&english);
    }
    initialized.store(true, std::memory_order_release);
}

QString activeUiLanguage()
{
    if (!initialized.load(std::memory_order_acquire))return QStringLiteral("en");
    return language;
}

QString uiText(const QString &source)
{
    if (!initialized.load(std::memory_order_acquire))return source;
    const auto utf8 = source.toUtf8();
    return QCoreApplication::translate("LeafyrinoUI", utf8.constData());
}

}  // namespace chatterino
