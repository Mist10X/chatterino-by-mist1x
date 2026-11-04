#include "common/UiLanguage.hpp"
#include "widgets/helper/ArchiveUserCard.hpp"

#include "Application.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "widgets/helper/ArchiveActions.hpp"
#include "widgets/helper/TabAvatars.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStringList>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino {
namespace {
// The card's own colours (UserInfoPopup::themeChangedEvent).
constexpr auto FOREGROUND = "#ded9ee";
constexpr auto MUTED = "#a19dad";
constexpr auto BORDER = "#333039";
constexpr auto ACCENT = "#b59be5";
constexpr auto FACTS = "#212229";
constexpr auto CHANNEL = "#ddd3f7";
constexpr int AVATAR = 18;
constexpr int ICON = 16;

using Icon = NoticeIconLayoutElement::Icon;

/// The colours of punishments in the chat (MessageLayout punishmentColor).
QColor kindColour(const QString &kind)
{
    if (kind == "ban")
        return {239, 68, 84};
    if (kind == "timeout")
        return {234, 179, 8};
    if (kind == "delete")
        return {150, 150, 160};
    if (kind == "automod")
        return {243, 180, 95};
    // lifted: unban, untimeout, a reward unmute
    return {134, 214, 176};
}
std::optional<Icon> kindIcon(const QString &kind)
{
    if (kind == "ban")
        return Icon::Ban;
    if (kind == "timeout")
        return Icon::Timeout;
    if (kind == "delete")
        return Icon::Deletion;
    return std::nullopt;
}
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
QString missing(const QJsonObject &block)
{
    const auto state = block.value("status").toString();
    if (state == "unavailable" || state == "unsupported" ||
        state == "unsupported_uid")
        return uiText(QStringLiteral("Источник не подготовлен или не "
                                     "поддерживается локальным API."));
    if (state == "not_covered")
        return uiText(QStringLiteral("Период не покрыт сбором."));
    return uiText(QStringLiteral("Сохранённых записей не найдено."));
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
QString plural(qint64 n, const QString &one, const QString &few,
               const QString &many)
{
    const auto tens = n % 100;
    const auto last = n % 10;
    if (tens >= 11 && tens <= 14)
        return many;
    if (last == 1)
        return one;
    if (last >= 2 && last <= 4)
        return few;
    return many;
}
/// What happened, worded like the chat's own notices.
QString eventLabel(const QJsonObject &row)
{
    const auto kind = row.value("kind").toString();
    if (kind == "ban")
        return uiText(QStringLiteral("Бан"));
    if (kind == "timeout")
    {
        const auto duration = durationText(row.value("duration").toInt());
        return duration.isEmpty()
                   ? uiText(QStringLiteral("Мут"))
                   : uiText(QStringLiteral("Мут · %1")).arg(duration);
    }
    if (kind == "delete")
        return uiText(QStringLiteral("Удаление"));
    if (kind == "automod")
        return QStringLiteral("AutoMod");
    if (kind == "unban")
        return uiText(QStringLiteral("Разбан"));
    if (kind == "untimeout")
        return uiText(QStringLiteral("Снят мут"));
    if (kind == "reward_unmute")
        return uiText(QStringLiteral("Размут"));
    return kind;
}
/// The message the punishment is about: the deleted or held one, otherwise
/// the person's last saved message before it.
QString detailText(const QJsonObject &row)
{
    const auto message = row.value("message").toObject();
    if (!message.value("text").toString().isEmpty())
        return message.value("text").toString();
    const auto context = row.value("context").toArray();
    for (auto i = context.size() - 1; i >= 0; --i)
    {
        const auto text = context.at(i).toObject().value("text").toString();
        if (!text.isEmpty())
            return text;
    }
    return {};
}
QString rgba(QColor colour, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(colour.red())
        .arg(colour.green())
        .arg(colour.blue())
        .arg(alpha);
}
QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(parent);
    // saved chat text is shown as text, never as markup
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    return label;
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
    , summary_(new QWidget(this))
    , tabs_(new QWidget(this))
    , punishmentsTab_(new QPushButton(uiText(QStringLiteral("Наказания")), this))
    , messagesTab_(new QPushButton(uiText(QStringLiteral("Сообщения")), this))
    , pages_(new QStackedWidget(this))
    , punishments_(new QScrollArea(this))
    , punishmentsList_(nullptr)
    , body_(new QTextBrowser(this))
    , history_(new QPushButton(uiText(QStringLiteral("Вся история")), this))
    , moderation_(new QPushButton(uiText(QStringLiteral("Наказания в Архиве")), this))
    , open_(new QPushButton(uiText(QStringLiteral("Открыть Архив")), this))
{
    this->setObjectName("archiveUserCard");
    this->status_->setObjectName("archiveStatus");
    this->summary_->setObjectName("archiveSummary");
    this->tabs_->setObjectName("archiveTabs");
    this->body_->setObjectName("archiveSavedContent");
    this->punishments_->setObjectName("archivePunishments");
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
    auto *chips = new QHBoxLayout(this->summary_);
    chips->setContentsMargins(10, 8, 10, 6);
    chips->setSpacing(6);
    layout->addWidget(this->summary_);
    // the card's own tabs ("Сообщения в чате" / "Архив") look the same
    auto *tabsLayout = new QHBoxLayout(this->tabs_);
    tabsLayout->setContentsMargins(4, 0, 4, 0);
    tabsLayout->setSpacing(0);
    for (auto *tab : {this->punishmentsTab_, this->messagesTab_})
    {
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tabsLayout->addWidget(tab);
    }
    tabsLayout->addStretch(1);
    layout->addWidget(this->tabs_);

    auto *list = new QWidget(this->punishments_);
    list->setObjectName("archivePunishmentsList");
    this->punishmentsList_ = new QVBoxLayout(list);
    this->punishmentsList_->setContentsMargins(8, 8, 8, 8);
    this->punishmentsList_->setSpacing(6);
    this->punishmentsList_->addStretch(1);
    this->punishments_->setWidget(list);
    this->punishments_->setWidgetResizable(true);
    this->punishments_->setFrameShape(QFrame::NoFrame);
    this->punishments_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->pages_->addWidget(this->punishments_);
    this->pages_->addWidget(this->body_);
    this->pages_->setCurrentWidget(this->body_);
    layout->addWidget(this->pages_, 1);

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
    this->moderation_->setToolTip(uiText(QStringLiteral(
        "Открыть в Архиве «Пользователи → этот ник → Наказания»")));
    this->open_->setToolTip(uiText(QStringLiteral("Открыть окно Архива")));
    this->setMinimumWidth(0);
    this->body_->setMinimumWidth(0);
    this->setStyleSheet(
        QStringLiteral(
            "#archiveUserCard{background:#131318;color:%1;}"
            "#archiveUserCard QLabel{color:%2;background:transparent;}"
            "#archiveUserCard QTextBrowser{background:#131318;color:%1;"
            "border:0;}"
            "#archivePunishments, #archivePunishmentsList"
            "{background:#131318;border:0;}"
            "#archiveActions{border-top:1px solid %3;}"
            "#archiveActions QPushButton{background:transparent;color:%4;"
            "border:0;padding:3px 6px;}"
            "#archiveActions QPushButton:disabled{color:#5f5a69;}"
            "#archiveActions QPushButton:hover{color:#d2c2f2;"
            "text-decoration:underline;}"
            "#archiveTabs QPushButton{background:transparent;color:%1;"
            "border:0;border-bottom:1px solid %3;padding:4px 10px;}"
            "#archiveTabs QPushButton:checked{color:%4;"
            "border-bottom-color:%4;}"
            "QLabel#archiveChip{background:%5;border:1px solid %3;"
            "border-radius:6px;padding:1px 7px;color:%1;}"
            "QLabel#archiveText{color:%1;}"
            "QLabel#archiveChannel{color:%6;}")
            .arg(FOREGROUND, MUTED, BORDER, ACCENT, FACTS, CHANNEL));
    QObject::connect(this->client_, &ArchiveClient::cardReady, this,
                     &ArchiveUserCard::render);
    QObject::connect(this->client_, &ArchiveClient::failed, this,
                     [this](const QString &code, const QString &message) {
                         this->status(message, code == "unavailable" ||
                                                       code == "not_configured"
                                                   ? "unavailable"
                                                   : "error");
                         this->clearData();
                         this->body_->setPlainText(uiText(QStringLiteral(
                             "Данные не получены. Это не означает, что история "
                             "пользователя пуста.")));
                     });
    QObject::connect(this->punishmentsTab_, &QPushButton::clicked, this,
                     [this] {
                         this->showTab(Tab::Punishments);
                     });
    QObject::connect(this->messagesTab_, &QPushButton::clicked, this, [this] {
        this->showTab(Tab::Messages);
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
    // Channel avatars come in after the first look.
    if (tryGetApp() != nullptr)
    {
        QObject::connect(
            &TabAvatars::instance(), &TabAvatars::loaded, this,
            [this](const QString &login) {
                const auto waiting = this->avatars_.take(login);
                const auto pixmap = TabAvatars::instance().circle(
                    login, int(AVATAR * this->devicePixelRatioF()));
                for (const auto &label : waiting)
                {
                    if (label && !pixmap.isNull())
                    {
                        auto scaled = pixmap;
                        scaled.setDevicePixelRatio(this->devicePixelRatioF());
                        label->setPixmap(scaled);
                    }
                }
            });
    }
    this->setTarget({}, {}, {});
}

void ArchiveUserCard::status(const QString &text, const QString &state)
{
    this->status_->setText(text);
    this->setProperty("archiveState", state);
    // Found data speaks for itself; other states explain the empty body.
    this->status_->setVisible(state != "found");
}

void ArchiveUserCard::clearData()
{
    this->punishmentsText_.clear();
    this->avatars_.clear();
    while (this->punishmentsList_->count() > 1)
    {
        auto *item = this->punishmentsList_->takeAt(0);
        delete item->widget();
        delete item;
    }
    for (auto *chip : this->summary_->findChildren<QLabel *>())
        chip->deleteLater();
    this->summary_->hide();
    this->tabs_->hide();
    this->body_->clear();
    this->pages_->setCurrentWidget(this->body_);
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
    this->clearData();
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
    this->clearData();
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

QLabel *ArchiveUserCard::avatarLabel(const QString &channel)
{
    auto *label = new QLabel;
    label->setFixedSize(AVATAR, AVATAR);
    if (tryGetApp() == nullptr || channel.isEmpty())
        return label;
    const auto login = channel.toLower();
    const auto ratio = this->devicePixelRatioF();
    auto pixmap = TabAvatars::instance().circle(login, int(AVATAR * ratio));
    if (pixmap.isNull())
    {
        this->avatars_[login].append(label);
        return label;
    }
    pixmap.setDevicePixelRatio(ratio);
    label->setPixmap(pixmap);
    return label;
}

void ArchiveUserCard::addPunishment(const QJsonObject &event, bool fromDossier)
{
    const auto kind = fromDossier ? QStringLiteral("ban")
                                  : event.value("kind").toString();
    // lifted later (an unban, the mute taken off): green, as in the Archive
    const auto lifted = event.value("lifted").toString();
    const auto colour = kindColour(lifted.isEmpty() ? kind : QString());

    auto *box = new QFrame;
    box->setObjectName("archivePunishment");
    // the soft box of punishments in the chat: tinted, a brighter edge
    box->setStyleSheet(QStringLiteral("QFrame#archivePunishment{background:%1;"
                                      "border:1px solid %2;border-radius:8px;}")
                           .arg(rgba(colour, 26), rgba(colour, 90)));
    auto *rows = new QVBoxLayout(box);
    rows->setContentsMargins(10, 7, 10, 8);
    rows->setSpacing(4);

    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    if (const auto icon = kindIcon(kind))
    {
        const auto ratio = this->devicePixelRatioF();
        QPixmap picture(int(ICON * ratio), int(ICON * ratio));
        picture.setDevicePixelRatio(ratio);
        picture.fill(Qt::transparent);
        QPainter painter(&picture);
        NoticeIconLayoutElement::paintIcon(painter, *icon, colour,
                                           QRectF(0, 0, ICON, ICON));
        painter.end();
        auto *iconLabel = new QLabel(box);
        iconLabel->setPixmap(picture);
        iconLabel->setFixedSize(ICON, ICON);
        top->addWidget(iconLabel);
    }
    const auto label =
        fromDossier ? uiText(QStringLiteral("Бан · досье"))
        : lifted.isEmpty()
            ? eventLabel(event)
            : eventLabel(event) + uiText(QStringLiteral(" · снят"));
    auto *what = plainLabel(label, box);
    what->setStyleSheet(QStringLiteral("color:%1;font-weight:600;")
                            .arg(colour.name()));
    top->addWidget(what);
    top->addSpacing(4);
    const auto channel = event.value("channel").toString();
    top->addWidget(this->avatarLabel(channel));
    auto *channelLabel = plainLabel(channel, box);
    channelLabel->setObjectName("archiveChannel");
    top->addWidget(channelLabel);
    top->addStretch(1);
    auto *time = plainLabel(shortWhen(event.value("at_ms")), box);
    top->addWidget(time);
    rows->addLayout(top);

    QString text;
    if (fromDossier)
    {
        text = uiText(QStringLiteral("Получено досье ")) +
               when(event.value("fetched_at_ms"));
        rows->addWidget(plainLabel(text, box));
    }
    else
    {
        text = detailText(event);
        auto *message = plainLabel(
            text.isEmpty() ? uiText(QStringLiteral("Сообщение недоступно"))
                           : text.left(500),
            box);
        message->setWordWrap(true);
        message->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (!text.isEmpty())
            message->setObjectName("archiveText");
        rows->addWidget(message);
        if (!lifted.isEmpty())
        {
            auto *how = plainLabel(
                lifted == "unban" ? uiText(QStringLiteral("Снят: событие разбана"))
                : lifted == "untimeout"
                    ? uiText(QStringLiteral("Снят: событие снятия мута"))
                    : uiText(QStringLiteral("Снят: пользователь снова пишет")),
                box);
            how->setStyleSheet(QStringLiteral("color:%1;").arg(colour.name()));
            rows->addWidget(how);
            text += '\n' + how->text();
        }
        const auto receipt = event.value("receipt").toObject();
        if (!receipt.isEmpty())
        {
            auto *automod = plainLabel(
                QStringLiteral("AutoMod · ") +
                    receipt.value("state").toString().left(80) +
                    uiText(" · проверка ") +
                    shortWhen(receipt.value("checked_at_ms")),
                box);
            automod->setStyleSheet(QStringLiteral("color:#81b9f4;"));
            rows->addWidget(automod);
            text += '\n' + automod->text();
        }
    }
    this->punishmentsList_->insertWidget(this->punishmentsList_->count() - 1,
                                         box);
    this->punishmentsText_ += label + ' ' + channel + ' ' + time->text() +
                              '\n' + text + '\n';
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
    this->clearData();

    const auto moderation = data.value("moderation").toObject();
    const auto messages = data.value("messages").toObject();
    const auto dossier = data.value("dossier").toObject();
    QJsonArray events;
    for (const auto &raw : moderation.value("rows").toArray())
    {
        // Secondary defense: identity is per record, never inferred from a nick.
        if (raw.toObject().value("user_id").toString() == this->userId_)
            events.append(raw);
    }

    // ---- the chips: messages and each kind of punishment ----
    QHash<QString, qint64> counts;
    const auto saved = moderation.value("counts").toObject();
    if (!saved.isEmpty())
    {
        for (auto it = saved.begin(); it != saved.end(); ++it)
            counts[it.key()] = it.value().toInteger();
    }
    else
    {
        for (const auto &raw : events)
            counts[raw.toObject().value("kind").toString()] += 1;
    }
    auto *chips = static_cast<QHBoxLayout *>(this->summary_->layout());
    while (chips->count() > 0)
        delete chips->takeAt(0);
    auto chip = [&](qint64 n, const QString &colour, const QString &one,
                    const QString &few, const QString &many) {
        if (n <= 0)
            return;
        auto *label = new QLabel(this->summary_);
        label->setObjectName("archiveChip");
        label->setTextFormat(Qt::RichText);
        label->setText(
            QStringLiteral("<span style='color:%1;font-weight:600;'>%2</span> %3")
                .arg(colour, QLocale(QLocale::Russian).toString(n),
                     plural(n, one, few, many)));
        chips->addWidget(label);
    };
    chip(messages.value("total").toInteger(), FOREGROUND,
         uiText(QStringLiteral("сообщение")), uiText(QStringLiteral("сообщения")),
         uiText(QStringLiteral("сообщений")));
    chip(counts.value("ban"), kindColour("ban").name(),
         uiText(QStringLiteral("бан")), uiText(QStringLiteral("бана")),
         uiText(QStringLiteral("банов")));
    chip(counts.value("timeout"), kindColour("timeout").name(),
         uiText(QStringLiteral("мут")), uiText(QStringLiteral("мута")),
         uiText(QStringLiteral("мутов")));
    chip(counts.value("delete"), kindColour("delete").name(),
         uiText(QStringLiteral("удаление")), uiText(QStringLiteral("удаления")),
         uiText(QStringLiteral("удалений")));
    chips->addStretch(1);
    this->summary_->setVisible(chips->count() > 1);

    // ---- Наказания ----
    for (const auto &raw : events)
        this->addPunishment(raw.toObject(), false);
    const auto bans = dossier.value("rows").toArray();
    for (qsizetype i = 0; i < std::min<qsizetype>(bans.size(), 8); ++i)
        this->addPunishment(bans[i].toObject(), true);
    QStringList quiet;
    if (events.isEmpty() && bans.isEmpty())
        quiet += worthShowing(moderation)
                     ? missing(moderation)
                     : uiText(QStringLiteral("Наказаний в Архиве нет."));
    if (worthShowing(dossier) && bans.isEmpty())
        quiet += missing(dossier);

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
        quiet += notes.join(QStringLiteral(" · "));
    for (const auto &line : quiet)
    {
        auto *label = plainLabel(line, nullptr);
        label->setWordWrap(true);
        label->setContentsMargins(2, 4, 2, 0);
        this->punishmentsList_->insertWidget(
            this->punishmentsList_->count() - 1, label);
        this->punishmentsText_ += line + '\n';
    }

    // ---- Сообщения: they read like the chat, grouped by day ----
    QString chat = QStringLiteral(
        "<style>"
        "p{margin:0;}"
        ".muted{color:%1;}"
        ".day{color:%1;font-size:small;margin:6px 0 2px 0;}"
        ".row{margin:2px 0;}"
        ".time{color:%1;}"
        ".chan{color:%1;}"
        "a{color:%2;text-decoration:none;}"
        "a.quiet{color:%1;}"
        "</style><div style='margin:6px 10px;'>")
                       .arg(MUTED, ACCENT);
    const auto rows = messages.value("rows").toArray();
    if (rows.isEmpty())
        chat += "<p class='muted'>" + missing(messages) + "</p>";
    QDate lastDay;
    for (qsizetype i = 0; i < std::min<qsizetype>(rows.size(), 20); ++i)
    {
        const auto message = rows[i].toObject();
        // Secondary defense: identity is per record, never inferred from a nick.
        if (message.value("user_id").toString() != this->userId_)
            continue;
        const auto at = localTime(message.value("at_ms"));
        if (at.isValid() && at.date() != lastDay)
        {
            lastDay = at.date();
            chat += "<p class='day' align='center'>" + dayTitle(lastDay) +
                    "</p>";
        }
        const auto time = at.isValid() ? at.toString("HH:mm")
                                       : QStringLiteral("--:--");
        const auto messageId = message.value("id").toString();
        const auto timeHtml =
            messageId.isEmpty()
                ? "<span class='time'>" + time + "</span>"
                : contextLink(messageId, time);
        const auto channel = message.value("channel").toString();
        const auto channelHtml =
            channel.isEmpty() ||
                    channel.compare(this->channel_, Qt::CaseInsensitive) == 0
                ? QString()
                : " <span class='chan'>#" + escaped(channel, 25) + "</span>";
        chat += "<p class='row'>" + timeHtml + channelHtml + "&nbsp; " +
                escaped(message.value("text")) + "</p>";
    }
    chat += "</div>";
    if (!notes.isEmpty())
        chat += "<p class='muted' style='margin:6px 10px;'>" +
                notes.join(QStringLiteral(" · ")).toHtmlEscaped() + "</p>";
    this->body_->setHtml(chat);

    this->tabs_->show();
    // Punishments first: that is what the card is opened for here.
    const bool anyPunishment = !events.isEmpty() || !bans.isEmpty();
    this->showTab(anyPunishment || rows.isEmpty() ? Tab::Punishments
                                                  : Tab::Messages);
}

void ArchiveUserCard::showTab(Tab tab)
{
    this->punishmentsTab_->setChecked(tab == Tab::Punishments);
    this->messagesTab_->setChecked(tab == Tab::Messages);
    auto *page = tab == Tab::Punishments
                     ? static_cast<QWidget *>(this->punishments_)
                     : static_cast<QWidget *>(this->body_);
    this->pages_->setCurrentWidget(page);
    this->punishments_->verticalScrollBar()->setValue(0);
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
    // both tabs: what the card knows, whichever is shown
    if (this->punishmentsText_.isEmpty())
        return this->body_->toPlainText();
    return this->punishmentsText_ + '\n' + this->body_->toPlainText();
}
}  // namespace chatterino
