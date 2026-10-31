// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/InputHighlighter.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/LinkParser.hpp"
#include "common/UiLanguage.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/spellcheck/SpellChecker.hpp"
#include "messages/Emote.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPersonalEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <cmath>
#include <utility>

namespace {

using namespace chatterino;

bool isEmote(TwitchChannel *twitch, KickChannel *kick, QStringView word)
{
    EmoteNameView name{word};
    if (twitch)
    {
        if (twitch->bttvEmote(name) || twitch->ffzEmote(name) ||
            twitch->seventvEmote(name))
        {
            return true;
        }
        auto locals = twitch->localTwitchEmotes();
        if (locals->contains(name))
        {
            return true;
        }
    }
    if (kick)
    {
        if (kick->seventvEmote(name))
        {
            return true;
        }

        auto globals = getApp()->getKickChatServer()->globalEmotes();
        if (globals->contains(name))
        {
            return true;
        }
    }

    if (getApp()->getBttvEmotes()->emote(name) ||
        getApp()->getFfzEmotes()->emote(name) ||
        getApp()->getSeventvEmotes()->globalEmote(name))
    {
        return true;
    }

    if (getApp()
            ->getAccounts()
            ->twitch.getCurrent()
            ->twitchEmote(name)
            .has_value())
    {
        return true;
    }

    return false;
}

EmotePtr findEmote(TwitchChannel *twitch, KickChannel *kick, QStringView word)
{
    EmoteNameView name{word};
    // your own 7TV personal emotes come first, as in sent messages
    if (auto personal =
            getApp()->getSeventvPersonalEmotes()->getEmoteForTwitchUser(
                getApp()->getAccounts()->twitch.getCurrent()->getUserId(),
                name))
    {
        return personal;
    }
    if (twitch)
    {
        for (auto emote : {twitch->seventvEmote(name), twitch->bttvEmote(name),
                           twitch->ffzEmote(name)})
        {
            if (emote)
            {
                return *emote;
            }
        }
        auto locals = twitch->localTwitchEmotes();
        if (auto it = locals->find(name); it != locals->end())
        {
            return it->second;
        }
    }
    if (kick)
    {
        if (auto emote = kick->seventvEmote(name))
        {
            return emote;
        }
        auto globals = getApp()->getKickChatServer()->globalEmotes();
        if (auto it = globals->find(name); it != globals->end())
        {
            return it->second;
        }
    }
    for (auto emote : {getApp()->getSeventvEmotes()->globalEmote(name),
                       getApp()->getBttvEmotes()->emote(name),
                       getApp()->getFfzEmotes()->emote(name),
                       getApp()->getAccounts()->twitch.getCurrent()->twitchEmote(
                           name)})
    {
        if (emote)
        {
            return *emote;
        }
    }
    return nullptr;
}

struct ShownEmote {
    int start;
    EmotePtr emote;
    QSizeF size;
    int length = 0;
};

class EmoteBlockData final : public QTextBlockUserData
{
public:
    std::vector<ShownEmote> emotes;
};

bool isChatter(TwitchChannel *twitch, KickChannel *kick, const QString &word)
{
    ChannelChatters *cc =
        twitch ? static_cast<ChannelChatters *>(twitch) : kick;
    Channel *c = twitch ? static_cast<Channel *>(twitch) : kick;
    if (cc)
    {
        if (cc->accessChatters()->contains(word) ||
            (getSettings()->alwaysIncludeBroadcasterInUserCompletions &&
             word.compare(c->getName(), Qt::CaseInsensitive) == 0))
        {
            return true;
        }
    }
    return false;
}

bool isLink(const QString &token)
{
    auto link = linkparser::parse(token);
    return link.has_value();
}

bool isIgnoredWord(TwitchChannel *twitch, KickChannel *kick,
                   const QString &word)
{
    return isEmote(twitch, kick, word) || isChatter(twitch, kick, word);
}

bool isIgnoredToken(TwitchChannel *twitch, KickChannel *kick,
                    const QString &token)
{
    return isEmote(twitch, kick, token) || isLink(token);
}

}  // namespace

namespace chatterino {

namespace inputhighlight::detail {

QRegularExpression wordRegex()
{
    static QRegularExpression regex{
        R"((?<=^|(?!_)\p{P})\p{L}+(?=$|(?!_)\p{P}))",
        QRegularExpression::PatternOption::UseUnicodePropertiesOption,
    };
    return regex;
}

}  // namespace inputhighlight::detail

InputHighlighter::InputHighlighter(SpellChecker &spellChecker, QObject *parent)
    : QSyntaxHighlighter(parent)
    , spellChecker(spellChecker)
    , wordRegex(inputhighlight::detail::wordRegex())
    , tokenRegex(R"(\S+)")
{
    this->spellFmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    this->spellFmt.setUnderlineColor(Qt::red);
}
InputHighlighter::~InputHighlighter() = default;

void InputHighlighter::setChannel(const std::shared_ptr<Channel> &channel)
{
    auto twitch = std::dynamic_pointer_cast<TwitchChannel>(channel);
    this->channel = twitch;
    auto kick = std::dynamic_pointer_cast<KickChannel>(channel);
    this->kickChannel = kick;
    this->rehighlight();
}

std::vector<QString> InputHighlighter::getSpellCheckedWords(const QString &text)
{
    std::vector<QString> words;
    this->visitWords(text, [&](const QString &word, qsizetype, qsizetype) {
        words.emplace_back(word);
    });
    return words;
}

QStringView InputHighlighter::getWordAt(QStringView text, qsizetype pos)
{
    auto tokenIt = this->tokenRegex.globalMatchView(text);
    QString token;
    qsizetype posInWord = 0;
    qsizetype tokenStart = 0;
    while (tokenIt.hasNext())
    {
        auto match = tokenIt.next();

        if (match.capturedStart() <= pos && pos <= match.capturedEnd())
        {
            token = match.captured();
            tokenStart = match.capturedStart();
            posInWord = pos - tokenStart;
            break;
        }
    }
    if (token.isEmpty())
    {
        return {};
    }

    QStringView word;
    this->visitWords(token,
                     [&](const QString &, qsizetype start, qsizetype count) {
                         if (start <= posInWord && posInWord <= start + count)
                         {
                             assert(word.isEmpty());
                             word = text.sliced(tokenStart + start, count);
                         }
                     });
    return word;
}

void InputHighlighter::setSpellCheckEnabled(bool enabled)
{
    this->spellCheck_ = enabled;
}

void InputHighlighter::setEmotePreviewEnabled(bool enabled)
{
    this->emotePreview_ = enabled;
}

bool InputHighlighter::needsDocument() const
{
    // the whisper chip is always on
    return true;
}

namespace {

QFont whisperChipFont(const QFont &base)
{
    auto font = base;
    if (font.pixelSize() > 0)
    {
        font.setPixelSize(std::max(9, int(std::round(font.pixelSize() * 0.86))));
    }
    else
    {
        font.setPointSizeF(font.pointSizeF() * 0.86);
    }
    return font;
}

QString whisperChipText(const QString &target)
{
    return uiText("DM to %1").arg(target);
}

}  // namespace

void InputHighlighter::highlightWhisper(const QString &text)
{
    if (this->currentBlock().blockNumber() != 0)
    {
        return;
    }
    // the name is followed by a space: it is typed out (or completed)
    static const QRegularExpression whisper(
        QStringLiteral("^/(?:w|whisper)\\s+@?([A-Za-z0-9_]{2,25})\\s"));
    const auto match = whisper.match(text);
    if (!match.hasMatch())
    {
        this->whisperPrefix_ = 0;
        this->whisperTarget_.clear();
        return;
    }
    this->whisperTarget_ = match.captured(1);
    this->whisperPrefix_ = int(match.capturedLength());

    // the prefix keeps its characters but is invisible and as wide as the chip
    const auto base = this->document()->defaultFont();
    const QFontMetricsF metrics(base);
    const QFontMetricsF chipMetrics(whisperChipFont(base));
    const qreal chip =
        chipMetrics.horizontalAdvance(whisperChipText(this->whisperTarget_)) +
        metrics.height() + 13;
    const auto prefix = match.captured(0);
    QTextCharFormat format;
    format.setForeground(Qt::transparent);
    format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    format.setFontLetterSpacing((chip + 6 - metrics.horizontalAdvance(prefix)) /
                                prefix.size());
    this->setFormat(0, this->whisperPrefix_, format);
}

void InputHighlighter::paintWhisperChip(QPainter &painter,
                                        const QTextEdit &edit) const
{
    if (this->whisperPrefix_ == 0 || this->document() == nullptr)
    {
        return;
    }
    QTextCursor start(this->document());
    start.setPosition(0);
    QTextCursor end(this->document());
    end.setPosition(this->whisperPrefix_);
    const auto from = edit.cursorRect(start);
    const auto to = edit.cursorRect(end);
    if (to.top() != from.top())
    {
        return;  // a very narrow input: no room for the chip on the line
    }
    const auto base = this->document()->defaultFont();
    const QFontMetricsF metrics(base);
    const QRectF chip(from.left() + 1, from.center().y() - metrics.height() / 2,
                      std::max<qreal>(10, to.left() - from.left() - 6),
                      metrics.height());
    const QColor teal(93, 202, 165);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto fill = teal;
    fill.setAlpha(34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(chip, 4, 4);

    // envelope
    const qreal side = chip.height() * 0.62;
    const QRectF icon(chip.left() + 6, chip.center().y() - side * 0.36,
                      side, side * 0.72);
    QPen pen(teal, 1.3);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(icon, 1.5, 1.5);
    painter.drawPolyline(QPolygonF{icon.topLeft() + QPointF(0.6, 0.8),
                                   QPointF(icon.center().x(),
                                           icon.top() + icon.height() * 0.55),
                                   icon.topRight() + QPointF(-0.6, 0.8)});

    painter.setFont(whisperChipFont(base));
    painter.setPen(teal);
    painter.drawText(chip.adjusted(icon.width() + 11, 0, -4, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     whisperChipText(this->whisperTarget_));

    // the hint at the right end, when the line leaves room for it
    QTextCursor last(this->document());
    last.movePosition(QTextCursor::End);
    const auto tail = edit.cursorRect(last);
    const auto hint = uiText("Esc — normal message");
    const QFontMetricsF hintMetrics(whisperChipFont(base));
    const qreal right = edit.viewport()->width() - 6;
    if (tail.top() == from.top() &&
        right - tail.right() > hintMetrics.horizontalAdvance(hint) + 16)
    {
        painter.setPen(edit.palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(tail.right(), chip.top(),
                                right - tail.right(), chip.height()),
                         Qt::AlignRight | Qt::AlignVCenter, hint);
    }
    painter.restore();
}

bool InputHighlighter::showsEmotes() const
{
    const auto *document = this->document();
    if (!document || !this->emotePreview_)
    {
        return false;
    }
    for (auto block = document->begin(); block.isValid(); block = block.next())
    {
        if (dynamic_cast<EmoteBlockData *>(block.userData()))
        {
            return true;
        }
    }
    return false;
}

bool InputHighlighter::takePendingSizes()
{
    return std::exchange(this->pendingSizes_, false);
}

void InputHighlighter::highlightEmotes(const QString &text)
{
    auto *previous = dynamic_cast<EmoteBlockData *>(this->currentBlockUserData());
    if (previous)
    {
        this->shownEmotes_ -= int(previous->emotes.size());
    }

    auto *twitch = this->channel.lock().get();
    auto *kick = this->kickChannel.lock().get();
    auto data = std::make_unique<EmoteBlockData>();
    const QFontMetricsF metrics(this->document()->defaultFont());
    const qreal height = std::round(metrics.height() * 1.4);
    // The hidden name of an emote is set in a font as tall as the emote: the
    // line then grows to fit it, instead of the emote spilling over the lines
    // around it (and over the input's edge).
    auto tall = this->document()->defaultFont();
    if (tall.pixelSize() > 0)
    {
        tall.setPixelSize(int(std::round(tall.pixelSize() * 1.4)));
    }
    else
    {
        tall.setPointSizeF(tall.pointSizeF() * 1.4);
    }
    const QFontMetricsF tallMetrics(tall);

    auto it = this->tokenRegex.globalMatch(text);
    while (it.hasNext())
    {
        const auto match = it.next();
        // The word still being typed stays text until a space or Tab follows.
        if (match.capturedEnd() >= text.size())
        {
            continue;
        }
        auto emote = findEmote(twitch, kick, match.capturedView());
        if (!emote)
        {
            continue;
        }
        const auto &image = emote->images.getImage1();
        if (!image->loaded())
        {
            // The text stays readable until the picture is there (and when it
            // never loads); a loaded image lays the line out again.
            std::ignore = image->pixmapOrLoad();
            this->pendingSizes_ = true;
            continue;
        }
        qreal width = height;
        if (image->width() > 0 && image->height() > 0)
        {
            width = std::min(height * 4,
                             height * image->width() / image->height());
        }
        else
        {
            this->pendingSizes_ = true;
        }

        // Keep the characters (copy, send, cursor) but make them invisible and
        // exactly as wide as the emote drawn over them.
        const auto length = int(match.capturedLength());
        QTextCharFormat format;
        format.setForeground(Qt::transparent);
        format.setFont(tall);
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(
            (width - tallMetrics.horizontalAdvance(match.captured())) /
            length);
        this->setFormat(int(match.capturedStart()), length, format);
        data->emotes.push_back({int(match.capturedStart()), emote,
                                QSizeF(width, height), int(length)});
    }

    this->shownEmotes_ += int(data->emotes.size());
    this->setCurrentBlockUserData(data->emotes.empty() ? nullptr
                                                       : data.release());
}

void InputHighlighter::paintEmotes(QPainter &painter,
                                   const QTextEdit &edit) const
{
    auto *document = this->document();
    if (!document || !this->emotePreview_)
    {
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto scale = float(std::max(1.0, edit.devicePixelRatioF() * 1.5));
    const auto selection = edit.textCursor();
    const auto highlight = edit.palette().color(QPalette::Highlight);
    for (auto block = document->begin(); block.isValid(); block = block.next())
    {
        const auto *data = dynamic_cast<EmoteBlockData *>(block.userData());
        if (!data)
        {
            continue;
        }
        for (const auto &shown : data->emotes)
        {
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + shown.start);
            const auto caret = edit.cursorRect(cursor);
            const QRectF target(caret.left() + 1,
                                caret.center().y() - shown.size.height() / 2,
                                shown.size.width(), shown.size.height());
            // A selection paints the hidden letters in a visible colour:
            // cover them with the selection colour under the emote.
            const auto from = block.position() + shown.start;
            if (selection.hasSelection() &&
                selection.selectionStart() < from + shown.length &&
                selection.selectionEnd() > from)
            {
                QTextCursor end(block);
                end.setPosition(from + shown.length);
                const auto right = edit.cursorRect(end).left();
                const auto line = edit.cursorRect(cursor);
                painter.fillRect(QRectF(caret.left(), line.top(),
                                        std::max<qreal>(1, right - caret.left()),
                                        line.height()),
                                 highlight);
            }
            const auto &image = shown.emote->images.getImageOrLoaded(scale);
            if (auto pixmap = image->pixmapOrLoad())
            {
                // keep the emote's own proportions inside the reserved box
                auto box = QSizeF(pixmap->size()).scaled(target.size(),
                                                         Qt::KeepAspectRatio);
                painter.drawPixmap(
                    QRectF(target.left() + (target.width() - box.width()) / 2,
                           target.top() + (target.height() - box.height()) / 2,
                           box.width(), box.height()),
                    *pixmap, QRectF(pixmap->rect()));
            }
        }
    }
}

void InputHighlighter::highlightBlock(const QString &text)
{
    if (this->emotePreview_)
    {
        this->highlightEmotes(text);
    }
    else if (this->currentBlockUserData())
    {
        this->shownEmotes_ -= int(
            dynamic_cast<EmoteBlockData *>(this->currentBlockUserData())
                ? dynamic_cast<EmoteBlockData *>(this->currentBlockUserData())
                      ->emotes.size()
                : 0);
        this->setCurrentBlockUserData(nullptr);
    }

    if (!this->spellCheck_ || !this->spellChecker.isLoaded())
    {
        this->highlightWhisper(text);
        return;
    }
    this->visitWords(
        text, [&](const QString &word, qsizetype start, qsizetype count) {
            if (!this->spellChecker.check(word))
            {
                this->setFormat(static_cast<int>(start),
                                static_cast<int>(count), this->spellFmt);
            }
        });
    this->highlightWhisper(text);
}

void InputHighlighter::visitWords(
    const QString &text,
    std::invocable<const QString &, qsizetype, qsizetype> auto &&cb)
{
    auto *channel = this->channel.lock().get();
    auto *kick = this->kickChannel.lock().get();

    QStringView textView = text;

    auto cmdTriggerLen = getApp()->getCommands()->commandTriggerLen(textView);
    textView = textView.sliced(cmdTriggerLen);

    auto tokenIt = this->tokenRegex.globalMatchView(textView);

    while (tokenIt.hasNext())
    {
        auto tokenMatch = tokenIt.next();
        auto token = tokenMatch.captured();
        if (isIgnoredToken(channel, kick, token))
        {
            continue;
        }

        auto wordIt = this->wordRegex.globalMatchView(token);

        while (wordIt.hasNext())
        {
            auto wordMatch = wordIt.next();
            auto word = wordMatch.captured();

            if (!isIgnoredWord(channel, kick, word))
            {
                cb(word,
                   static_cast<int>(cmdTriggerLen + tokenMatch.capturedStart() +
                                    wordMatch.capturedStart()),
                   static_cast<int>(word.size()));
            }
        }
    }
}

}  // namespace chatterino

