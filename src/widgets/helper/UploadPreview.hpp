// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>

#include <functional>

class QLabel;
class QPushButton;

namespace chatterino {

class ResizingTextEdit;
struct ImageUploadUpdate;

/// Small card at the bottom of the chat, right above the input, while a
/// pasted picture is uploaded and its link sits in the input: the picture,
/// the link and a button that deletes it from the site again.
class UploadPreview : public QFrame
{
public:
    /// @param view  the chat the card floats over (at its bottom)
    /// @param notify  shows a note in the chat, e.g. "deleted"
    UploadPreview(QWidget *view, ResizingTextEdit *textEdit,
                  std::function<void(const QString &)> notify);

    void apply(const ImageUploadUpdate &update);

    /// For the offscreen checks.
    const QString &link() const
    {
        return this->link_;
    }
    QPushButton *deleteButton() const
    {
        return this->delete_;
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void setStatus(const QString &title, const QString &detail);
    void deleteImage();
    void removeLinkFromInput();
    void onTextChanged();
    void updateStyle();
    void place();
    void close();

    QWidget *view_;
    QPointer<ResizingTextEdit> textEdit_;
    std::function<void(const QString &)> notify_;

    QLabel *thumbnail_;
    QLabel *title_;
    QLabel *detail_;
    QPushButton *delete_;
    QPushButton *close_;

    QString link_;
    /// Both lines before they are cut to the card's width.
    QString titleText_;
    QString detailText_;
    bool deleting_ = false;
};

}  // namespace chatterino
