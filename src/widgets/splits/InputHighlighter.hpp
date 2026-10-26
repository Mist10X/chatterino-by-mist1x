// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QRegularExpression>
#include <QString>
#include <QSyntaxHighlighter>

#include <concepts>
#include <memory>

class QPainter;
class QTextDocument;
class QTextEdit;

namespace chatterino {

class Channel;
class TwitchChannel;
class KickChannel;
class SpellChecker;

namespace inputhighlight::detail {

QRegularExpression wordRegex();

}

class InputHighlighter : public QSyntaxHighlighter
{
public:
    InputHighlighter(SpellChecker &spellChecker, QObject *parent);
    ~InputHighlighter() override;
    InputHighlighter(const InputHighlighter &) = delete;
    InputHighlighter(InputHighlighter &&) = delete;
    InputHighlighter &operator=(const InputHighlighter &) = delete;
    InputHighlighter &operator=(InputHighlighter &&) = delete;

    void setChannel(const std::shared_ptr<Channel> &channel);

    std::vector<QString> getSpellCheckedWords(const QString &text);

    QStringView getWordAt(QStringView text, qsizetype pos);

    void setSpellCheckEnabled(bool enabled);
    /// Emote names followed by a space (typed or Tab-completed) are shown as
    /// the emote. The text itself is untouched: only its look changes.
    void setEmotePreviewEnabled(bool enabled);
    bool needsDocument() const;

    /// Draws the emotes over their invisible, re-spaced names.
    void paintEmotes(QPainter &painter, const QTextEdit &edit) const;
    /// Whether any emote is shown (repaint for animation then).
    bool showsEmotes() const;
    /// An emote image got its size after the line was laid out.
    bool takePendingSizes();

protected:
    void highlightBlock(const QString &text) override;

private:
    void highlightEmotes(const QString &text);

    void visitWords(
        const QString &text,
        std::invocable<const QString &, qsizetype, qsizetype> auto &&cb);

    SpellChecker &spellChecker;
    QTextCharFormat spellFmt;

    std::weak_ptr<TwitchChannel> channel;
    std::weak_ptr<KickChannel> kickChannel;

    QRegularExpression wordRegex;
    QRegularExpression tokenRegex;

    bool spellCheck_ = false;
    bool emotePreview_ = false;
    bool pendingSizes_ = false;
    int shownEmotes_ = 0;
};

}  // namespace chatterino
