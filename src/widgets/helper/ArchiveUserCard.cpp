#include "common/UiLanguage.hpp"
#include "widgets/helper/ArchiveUserCard.hpp"

#include "widgets/helper/ArchiveActions.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTextBrowser>
#include <QTextOption>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino {
namespace {
QString escaped(const QJsonValue &value, int max = 4096)
{
    return value.toString().left(max).toHtmlEscaped().replace('\n', "<br>");
}
QString when(const QJsonValue &value)
{
    const auto ms = value.toInteger(-1);
    return ms > 0 && ms <= 253402300799999LL
               ? QDateTime::fromMSecsSinceEpoch(ms).toString(
                     "dd.MM.yyyy HH:mm:ss")
               : uiText(QStringLiteral("время не сохранено"));
}
QString heading(const QString &title, const QJsonObject &block)
{
    QString total;
    if (block.value("total").isDouble())
        total = QStringLiteral(" · ") +
                QLocale(QLocale::Russian)
                    .toString(block.value("total").toInteger());
    return "<p class='section'>" + title + total + "</p>";
}
QString missing(const QJsonObject &block)
{
    const auto state = block.value("status").toString();
    if (state == "unavailable" || state == "unsupported" ||
        state == "unsupported_uid")
        return uiText(QStringLiteral("<p class='muted'>Источник не подготовлен или не "
                              "поддерживается локальным API.</p>"));
    if (state == "not_covered")
        return uiText(QStringLiteral("<p class='muted'>Период не покрыт сбором.</p>"));
    return uiText(QStringLiteral(
        "<p class='muted'>Сохранённых записей не найдено.</p>"));
}
/// Empty sections are left out unless the source itself is missing.
bool worthShowing(const QJsonObject &block)
{
    const auto state = block.value("status").toString();
    return !block.value("rows").toArray().isEmpty() ||
           (state != "empty" && state != "ok" && !state.isEmpty());
}
QDateTime localTime(const QJsonValue &value)
{
    const auto ms = value.toInteger(-1);
    if (ms <= 0 || ms > 253402300799999LL)
        return {};
    return QDateTime::fromMSecsSinceEpoch(ms);
}
QString dayTitle(const QDate &day)
{
    const auto today = QDate::currentDate();
    if (day == today)
        return uiText(QStringLiteral("сегодня"));
    if (day == today.addDays(-1))
        return uiText(QStringLiteral("вчера"));
    return QLocale(QLocale::Russian)
        .toString(day, day.year() == today.year() ? QStringLiteral("d MMMM")
                                                  : QStringLiteral("d MMMM yyyy"));
}
QString shortWhen(const QJsonValue &value)
{
    const auto time = localTime(value);
    if (!time.isValid())
        return uiText(QStringLiteral("время не сохранено"));
    return time.date() == QDate::currentDate()
               ? uiText(QStringLiteral("сегодня ")) + time.toString("HH:mm")
               : time.toString("dd.MM.yyyy HH:mm");
}
QString durationText(int seconds)
{
    if (seconds <= 0)
        return {};
    if (seconds % 86400 == 0)
        return uiText(QStringLiteral("%1 д")).arg(seconds / 86400);
    if (seconds % 3600 == 0)
        return uiText(QStringLiteral("%1 ч")).arg(seconds / 3600);
    if (seconds % 60 == 0)
        return uiText(QStringLiteral("%1 мин")).arg(seconds / 60);
    return uiText(QStringLiteral("%1 с")).arg(seconds);
}
QString contextLink(const QString &messageId, const QString &label)
{
    QUrl url("archive-local://context");
    QUrlQuery query;
    query.addQueryItem("id", messageId);
    url.setQuery(query);
    return "<a class='quiet' href='" + url.toString().toHtmlEscaped() + "'>" +
           label + "</a>";
}
}  // namespace

ArchiveUserCard::ArchiveUserCard(QWidget *parent)
    : ArchiveUserCard(configuredArchiveEndpoint(), parent)
{
}

ArchiveUserCard::ArchiveUserCard(ArchiveEndpoint endpoint, QWidget *parent)
    : QWidget(parent)
    , endpoint_(std::move(endpoint))
    , client_(new ArchiveClient(this->endpoint_, this))
    , status_(new QLabel(this))
    , body_(new QTextBrowser(this))
    , history_(new QPushButton(uiText(QStringLiteral("Вся история")), this))
    , moderation_(new QPushButton(uiText(QStringLiteral("Наказания")), this))
    , open_(new QPushButton(uiText(QStringLiteral("Открыть Архив")), this))
{
    this->setObjectName("archiveUserCard");
    this->status_->setObjectName("archiveStatus");
    this->body_->setObjectName("archiveSavedContent");
    this->status_->setWordWrap(true);
    this->body_->setOpenLinks(false);
    this->body_->setOpenExternalLinks(false);
    this->body_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->body_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    this->body_->setFrameShape(QFrame::NoFrame);
    this->setMouseTracking(true);
    this->body_->viewport()->setMouseTracking(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    this->status_->setContentsMargins(10, 8, 10, 0);
    layout->addWidget(this->status_);
    layout->addWidget(this->body_, 1);
    // Link-like actions in one line, as in the card header.
    auto *bar = new QWidget(this);
    bar->setObjectName("archiveActions");
    bar->setMouseTracking(true);
    auto *buttons = new QHBoxLayout(bar);
    buttons->setContentsMargins(6, 3, 6, 3);
    buttons->setSpacing(2);
    for (auto *button : {this->history_, this->moderation_, this->open_})
    {
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addWidget(bar);
    this->history_->setToolTip(
        uiText(QStringLiteral("Все сохранённые сообщения пользователя в Архиве")));
    this->moderation_->setToolTip(
        uiText(QStringLiteral("Баны, муты и удаления этого пользователя в Архиве")));
    this->open_->setToolTip(uiText(QStringLiteral("Открыть окно Архива")));
    this->setMinimumWidth(0);
    this->body_->setMinimumWidth(0);
    // Same colours as the card's chat column.
    this->setStyleSheet(
        "#archiveUserCard{background:#131318;color:#ded9ee;}"
        "#archiveUserCard QLabel{color:#a19dad;}"
        "#archiveUserCard QTextBrowser{background:#131318;color:#ded9ee;"
        "border:0;}"
        "#archiveActions{border-top:1px solid #333039;}"
        "#archiveUserCard QPushButton{background:transparent;color:#b59be5;"
        "border:0;padding:3px 6px;}"
        "#archiveUserCard QPushButton:disabled{color:#5f5a69;}"
        "#archiveUserCard QPushButton:hover{color:#d2c2f2;"
        "text-decoration:underline;}");
    QObject::connect(this->client_, &ArchiveClient::cardReady, this,
                     &ArchiveUserCard::render);
    QObject::connect(this->client_, &ArchiveClient::failed, this,
                     [this](const QString &code, const QString &message) {
                         this->status(message, code == "unavailable" ||
                                                       code == "not_configured"
                                                   ? "unavailable"
                                                   : "error");
                         this->body_->setPlainText(uiText(QStringLiteral(
                             "Данные не получены. Это не означает, что история "
                             "пользователя пуста.")));
                     });
    QObject::connect(this->history_, &QPushButton::clicked, this, [this] {
        this->open("user");
    });
    QObject::connect(this->moderation_, &QPushButton::clicked, this, [this] {
        this->open("moderation");
    });
    QObject::connect(this->open_, &QPushButton::clicked, this, [this] {
        this->open("archive");
    });
    QObject::connect(this->body_, &QTextBrowser::anchorClicked, this,
                     [this](const QUrl &url) {
                         if (url.scheme() != "archive-local")
                             return;
                         const QUrlQuery query(url);
                         this->open(url.host(), query.queryItemValue("id"));
                     });
    this->setTarget({}, {}, {});
}

void ArchiveUserCard::status(const QString &text, const QString &state)
{
    this->status_->setText(text);
    this->setProperty("archiveState", state);
    // Found data speaks for itself; other states explain the empty body.
    this->status_->setVisible(state != "found");
}

void ArchiveUserCard::setIdentityUnavailable()
{
    // A failed public profile lookup must not replace a known-UID local result.
    if (!this->userId_.isEmpty())
        return;
    this->client_->cancel();
    this->status(
        uiText(QStringLiteral("Не удалось подтвердить Twitch ID пользователя.")),
        "identity_unavailable");
    this->body_->setPlainText(uiText(QStringLiteral(
        "История не запрашивалась: определение пользователя только по нику "
        "может смешать сведения о разных Twitch ID.")));
}

void ArchiveUserCard::setTarget(const QString &userId, const QString &login,
                                const QString &channel)
{
    if (!userId.isEmpty() && this->userId_ == userId && this->login_ == login &&
        this->channel_ == channel)
        return;
    this->client_->cancel();
    this->userId_ = userId;
    this->login_ = login;
    this->channel_ = channel;
    this->body_->clear();
    const bool known = !userId.isEmpty() && !login.isEmpty();
    const bool configured = this->client_->configured();
    for (auto *button : {this->history_, this->moderation_, this->open_})
        button->setEnabled(known && configured);
    if (!configured)
    {
        this->status(ArchiveClient::userFacingError("not_configured"),
                     "unavailable");
        return;
    }
    if (!known)
    {
        this->status(uiText(QStringLiteral("Уточняется Twitch ID пользователя…")),
                     "loading");
        return;
    }
    this->status(uiText(QStringLiteral("Загрузка сохранённых данных…")), "loading");
    this->client_->requestCard(userId, login, channel);
}

void ArchiveUserCard::render(const QJsonObject &data)
{
    // The client already verifies this, but the UI also refuses accidental
    // cross-user delivery from a future caller or a stale queued signal.
    if (data.value("identity").toObject().value("user_id").toString() !=
        this->userId_)
        return;
    const auto coverage = data.value("coverage").toObject();
    const auto coverageState = coverage.value("status").toString();
    const auto state = data.value("state").toString();
    this->status(coverageState == "not_covered"
                     ? uiText(QStringLiteral("Период не покрыт сбором"))
                 : state == "empty" ? uiText(QStringLiteral("Сохранённых данных нет"))
                 : state == "partial"
                     ? uiText(QStringLiteral("Неполная выдача: один из источников "
                                      "не ответил"))
                     : uiText(QStringLiteral("Данные найдены в локальном Архиве")),
                 coverageState == "not_covered" ? "not_covered" : state);
    QString html = QStringLiteral(
        "<style>"
        "p{margin:0;}"
        ".section{color:#a19dad;font-size:small;font-weight:600;"
        "margin:10px 0 4px 0;letter-spacing:1px;}"
        ".muted{color:#8e8a99;}"
        ".day{color:#8e8a99;font-size:small;margin:6px 0 2px 0;}"
        ".row{margin:2px 0;}"
        ".time{color:#8e8a99;}"
        ".chan{color:#8e8a99;}"
        ".ban{color:#ef5f5f;font-weight:700;}"
        ".mute{color:#efa65f;font-weight:700;}"
        ".del{color:#a19dad;font-weight:700;}"
        ".quote{color:#c9c4d6;margin:1px 0 0 14px;}"
        ".am{color:#81b9f4;}"
        "a{color:#b59be5;text-decoration:none;}"
        "a.quiet{color:#8e8a99;}"
        "</style>");

    // Punishments first: that is what a usercard is opened for.
    const auto moderation = data.value("moderation").toObject();
    const auto events = moderation.value("rows").toArray();
    if (worthShowing(moderation))
    {
        html += heading(uiText(QStringLiteral("НАКАЗАНИЯ")), moderation);
        if (events.isEmpty())
            html += missing(moderation);
    }
    for (qsizetype i = 0; i < std::min<qsizetype>(events.size(), 20); ++i)
    {
        const auto row = events[i].toObject();
        if (row.value("user_id").toString() != this->userId_)
            continue;
        const auto kind = row.value("kind").toString();
        QString tag;
        if (kind == "ban")
            tag = uiText(QStringLiteral("<span class='ban'>БАН</span>"));
        else if (kind == "timeout")
            tag = uiText(QStringLiteral("<span class='mute'>МУТ %1</span>"))
                      .arg(durationText(row.value("duration").toInt()));
        else if (kind == "delete")
            tag = uiText(QStringLiteral("<span class='del'>УДАЛЕНО</span>"));
        else
            tag = "<b>" + kind.toHtmlEscaped() + "</b>";
        html += "<p class='row'>" + tag + " <span class='chan'>#" +
                escaped(row.value("channel"), 25) + " · " +
                shortWhen(row.value("at_ms")) + "</span></p>";
        const auto deleted = row.value("message").toObject();
        if (!deleted.isEmpty())
            html += "<p class='quote'>" + escaped(deleted.value("text"), 500) +
                    "</p>";
        const auto receipt = row.value("receipt").toObject();
        if (!receipt.isEmpty())
            html += "<p class='quote muted'><span class='am'>AutoMod</span> · " +
                    escaped(receipt.value("state"), 80) + uiText(" · проверка ") +
                    shortWhen(receipt.value("checked_at_ms")) + "</p>";
    }

    const auto dossier = data.value("dossier").toObject();
    const auto bans = dossier.value("rows").toArray();
    if (worthShowing(dossier))
    {
        html += heading(uiText(QStringLiteral("ОБЩЕЕ ДОСЬЕ")), dossier);
        if (bans.isEmpty())
            html += missing(dossier);
    }
    for (qsizetype i = 0; i < std::min<qsizetype>(bans.size(), 8); ++i)
    {
        const auto row = bans[i].toObject();
        html += uiText("<p class='row'><span class='ban'>БАН</span> <span "
                "class='chan'>#") +
                escaped(row.value("channel"), 25) + " · " +
                shortWhen(row.value("at_ms")) +
                uiText("</span></p><p class='quote muted'>Получено досье ") +
                when(row.value("fetched_at_ms")) + "</p>";
    }

    // Messages read like the chat: time and text, grouped by day.
    const auto messages = data.value("messages").toObject();
    const auto rows = messages.value("rows").toArray();
    html += heading(uiText(QStringLiteral("СООБЩЕНИЯ")), messages);
    if (rows.isEmpty())
        html += missing(messages);
    QDate lastDay;
    for (qsizetype i = 0; i < std::min<qsizetype>(rows.size(), 20); ++i)
    {
        const auto row = rows[i].toObject();
        // Secondary defense: identity is per record, never inferred from a nick.
        if (row.value("user_id").toString() != this->userId_)
            continue;
        const auto at = localTime(row.value("at_ms"));
        if (at.isValid() && at.date() != lastDay)
        {
            lastDay = at.date();
            html += "<p class='day' align='center'>" + dayTitle(lastDay) +
                    "</p>";
        }
        const auto time = at.isValid() ? at.toString("HH:mm")
                                       : QStringLiteral("--:--");
        const auto messageId = row.value("id").toString();
        const auto timeHtml =
            messageId.isEmpty()
                ? "<span class='time'>" + time + "</span>"
                : contextLink(messageId, time);
        const auto channel = row.value("channel").toString();
        const auto channelHtml =
            channel.isEmpty() ||
                    channel.compare(this->channel_, Qt::CaseInsensitive) == 0
                ? QString()
                : " <span class='chan'>#" + escaped(channel, 25) + "</span>";
        html += "<p class='row'>" + timeHtml + channelHtml + "&nbsp; " +
                escaped(row.value("text")) + "</p>";
    }

    // Everything the reader should keep in mind, in one quiet line.
    QStringList notes;
    if (coverageState == "unknown")
        notes += uiText(QStringLiteral("Покрытие неизвестно: здесь только то, что "
                                "видел Архив"));
    else if (coverage.value("since_ms").toDouble() > 0)
        notes += uiText(QStringLiteral("Наблюдение с ")) +
                 shortWhen(coverage.value("since_ms"));
    if (data.value("truncated").toBool() || messages.value("more").toBool() ||
        moderation.value("more").toBool() || dossier.value("more").toBool())
        notes += uiText(QStringLiteral("Показана часть записей — остальное в Архиве"));
    if (!events.isEmpty())
        notes += uiText(QStringLiteral("контекст ≠ причина наказания"));
    if (!notes.isEmpty())
        html += "<p class='muted' style='margin-top:10px;'>" +
                notes.join(QStringLiteral(" · ")) + "</p>";
    this->body_->setHtml(html);
    this->body_->verticalScrollBar()->setValue(0);
}

void ArchiveUserCard::open(const QString &view, const QString &messageId)
{
    openArchiveView(this, this->endpoint_, view, this->userId_, this->login_,
                    this->channel_, messageId);
}

QString ArchiveUserCard::statusText() const
{
    return this->status_->text();
}
QString ArchiveUserCard::plainText() const
{
    return this->body_->toPlainText();
}
}  // namespace chatterino
