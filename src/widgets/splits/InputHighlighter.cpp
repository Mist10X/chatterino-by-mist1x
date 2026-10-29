// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/InputHighlighter.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/LinkParser.hpp"
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
    return this->spellCheck_ || this->emotePreview_;
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
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(
            (width - metrics.horizontalAdvance(match.captured())) / length);
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

